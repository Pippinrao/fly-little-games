// C2b — consistency guards for the application-frame type tables.
//
// This test is the single-source-of-truth guard for the two hand-authored
// tables in app_frame.hpp/cpp:
//
//   1. The frame-type-tag table (ObjectKind namespace) must equal the schema's
//      `kinds` array exactly, and the message namespace must equal the schema's
//      `messages` array order exactly. A schema re-ordering or addition must
//      fail here loudly instead of silently remapping wire values.
//   2. Every golden-corpus entry must round-trip through the framer: the entry's
//      tag must map to that directory's type.txt, and a framed legal.bin must
//      parse back to that type name and those exact bytes, with the published
//      hash, and with the per-channel allow-list agreeing with the parse result.
//
// Framing does not change any validator verdict: for legal.bin and for every
// negative golden the framed identification status must equal the status of
// check() on the naked body.

#include "app_frame.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using flynes::session::wire::AppFrame;
using flynes::session::wire::FrameTypeNamespace;
using flynes::session::wire::QuicChannel;
using flynes::session::wire::Status;

namespace {

int failures = 0;
int checks_run = 0;

void expect(bool condition, const std::string& message)
{
    ++checks_run;
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::string status_name(Status status)
{
    switch (status)
    {
    case Status::Ok:
        return "ok";
    case Status::Truncated:
        return "truncated";
    case Status::Trailing:
        return "trailing";
    case Status::UnknownEnum:
        return "unknown_enum";
    case Status::NonzeroReserved:
        return "nonzero_reserved";
    case Status::BadLength:
        return "bad_length";
    case Status::UnknownKind:
        return "unknown_tag";
    case Status::UnknownCriticalTag:
        return "unknown_critical";
    case Status::InvalidField:
        return "invalid_field";
    }
    return "?";
}

bool read_all(const fs::path& path, std::vector<std::uint8_t>* out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    in.seekg(0, std::ios::end);
    const auto end = in.tellg();
    if (end < 0)
        return false;
    in.seekg(0, std::ios::beg);
    out->assign(static_cast<std::size_t>(end), 0);
    if (end != 0 && !in.read(reinterpret_cast<char*>(out->data()), end))
        return false;
    return true;
}

std::string read_text(const fs::path& path)
{
    std::ifstream in(path);
    std::string line;
    std::getline(in, line);
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
        line.pop_back();
    return line;
}

std::string read_file(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::string text;
    if (!in)
        return text;
    in.seekg(0, std::ios::end);
    const auto end = in.tellg();
    if (end < 0)
        return text;
    in.seekg(0, std::ios::beg);
    text.resize(static_cast<std::size_t>(end));
    if (end != 0)
        in.read(&text[0], end);
    return text;
}

std::string hex_of(const std::uint8_t* bytes, std::size_t size)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string out(size * 2u, '0');
    for (std::size_t i = 0; i < size; ++i)
    {
        out[i * 2u] = digits[bytes[i] >> 4u];
        out[i * 2u + 1u] = digits[bytes[i] & 0x0Fu];
    }
    return out;
}

// --- minimal JSON scanning -------------------------------------------------
// The schema and the golden manifest are generated documents with a stable key
// order; these helpers only need to locate a top-level array and the string
// fields of its depth-1 elements. They are string-aware so that brackets and
// braces inside `notes` cannot confuse the depth tracking.

std::size_t skip_string(const std::string& text, std::size_t pos)
{
    // pos points at the opening quote; returns the index just past the close.
    ++pos;
    while (pos < text.size())
    {
        if (text[pos] == '\\')
        {
            pos += 2u;
            continue;
        }
        if (text[pos] == '"')
            return pos + 1u;
        ++pos;
    }
    return text.size();
}

bool array_body(const std::string& text, const char* key, std::string* out)
{
    const std::string needle = std::string("\"") + key + "\"";
    const std::size_t key_pos = text.find(needle);
    if (key_pos == std::string::npos)
        return false;
    const std::size_t colon = text.find(':', key_pos + needle.size());
    if (colon == std::string::npos)
        return false;
    std::size_t pos = colon + 1u;
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\n' || text[pos] == '\r' ||
                                 text[pos] == '\t'))
        ++pos;
    if (pos >= text.size() || text[pos] != '[')
        return false;
    const std::size_t begin = pos + 1u;
    int depth = 1;
    ++pos;
    while (pos < text.size())
    {
        const char c = text[pos];
        if (c == '"')
        {
            pos = skip_string(text, pos);
            continue;
        }
        if (c == '[' || c == '{')
            ++depth;
        else if (c == ']' || c == '}')
        {
            --depth;
            if (depth == 0)
            {
                *out = text.substr(begin, pos - begin);
                return true;
            }
        }
        ++pos;
    }
    return false;
}

std::vector<std::string> split_elements(const std::string& body)
{
    std::vector<std::string> out;
    int depth = 0;
    std::size_t begin = 0u;
    for (std::size_t pos = 0u; pos < body.size();)
    {
        const char c = body[pos];
        if (c == '"')
        {
            pos = skip_string(body, pos);
            continue;
        }
        if (c == '{')
        {
            if (depth == 0)
                begin = pos;
            ++depth;
        }
        else if (c == '}')
        {
            --depth;
            if (depth == 0)
                out.push_back(body.substr(begin, pos - begin + 1u));
        }
        ++pos;
    }
    return out;
}

// Returns the raw text of the value of `key` at the TOP LEVEL of the object.
// Nested objects (children/enums/fields) also contain keys, and wire fields are
// sometimes literally named "kind"/"name", so a plain substring search is not
// enough: this walks the object with string awareness and a depth counter.
bool top_level_value(const std::string& object, const char* key, std::string* out)
{
    const std::size_t open = object.find('{');
    if (open == std::string::npos)
        return false;
    int depth = 0;
    std::size_t pos = open + 1u; // keys of THIS object are at depth 0
    while (pos < object.size())
    {
        const char c = object[pos];
        if (c == '"')
        {
            const std::size_t end = skip_string(object, pos);
            const std::string token = object.substr(pos + 1u, end - pos - 2u);
            std::size_t after = end;
            while (after < object.size() && (object[after] == ' ' || object[after] == '\n' ||
                                             object[after] == '\r' || object[after] == '\t'))
                ++after;
            if (depth == 0 && token == key && after < object.size() && object[after] == ':')
            {
                std::size_t value = after + 1u;
                while (value < object.size() && (object[value] == ' ' || object[value] == '\n' ||
                                                 object[value] == '\r' || object[value] == '\t'))
                    ++value;
                const std::size_t begin = value;
                if (value < object.size() && object[value] == '"')
                {
                    const std::size_t value_end = skip_string(object, value);
                    *out = object.substr(value, value_end - value); // raw, quotes included
                    return true;
                }
                if (value < object.size() && (object[value] == '{' || object[value] == '['))
                {
                    int nested = 0;
                    while (value < object.size())
                    {
                        const char vc = object[value];
                        if (vc == '"')
                        {
                            value = skip_string(object, value);
                            continue;
                        }
                        if (vc == '{' || vc == '[')
                            ++nested;
                        else if (vc == '}' || vc == ']')
                        {
                            --nested;
                            if (nested == 0)
                            {
                                ++value;
                                break;
                            }
                        }
                        ++value;
                    }
                    *out = object.substr(begin, value - begin);
                    return true;
                }
                while (value < object.size() && object[value] != ',' && object[value] != '}')
                    ++value;
                *out = object.substr(begin, value - begin);
                return true;
            }
            pos = end;
            continue;
        }
        if (c == '{' || c == '[')
            ++depth;
        else if (c == '}' || c == ']')
            --depth;
        ++pos;
    }
    return false;
}

bool top_level_string(const std::string& object, const char* key, std::string* out)
{
    std::string value;
    if (!top_level_value(object, key, &value))
        return false;
    if (value.size() < 2u || value.front() != '"' || value.back() != '"')
        return false;
    *out = value.substr(1u, value.size() - 2u);
    return true;
}

bool top_level_number(const std::string& object, const char* key, std::size_t* out)
{
    std::string value;
    if (!top_level_value(object, key, &value))
        return false;
    if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
        return false;
    *out = static_cast<std::size_t>(std::stoul(value));
    return true;
}

bool parse_hex_kind(const std::string& text, std::uint16_t* out)
{
    if (text.size() != 6u || text[0] != '0' || text[1] != 'x')
        return false;
    std::uint32_t value = 0u;
    for (std::size_t i = 2u; i < text.size(); ++i)
    {
        const char c = text[i];
        std::uint32_t digit = 0u;
        if (c >= '0' && c <= '9')
            digit = static_cast<std::uint32_t>(c - '0');
        else if (c >= 'a' && c <= 'f')
            digit = static_cast<std::uint32_t>(c - 'a') + 10u;
        else if (c >= 'A' && c <= 'F')
            digit = static_cast<std::uint32_t>(c - 'A') + 10u;
        else
            return false;
        value = (value << 4u) | digit;
    }
    *out = static_cast<std::uint16_t>(value);
    return true;
}

// --- corpus model ----------------------------------------------------------

struct ManifestCase
{
    std::string name;
    bool has_kind = false;
    std::string kind;
    bool has_message = false;
    std::string message;
    std::size_t length = 0u;
};

std::vector<std::uint8_t> framed(std::uint16_t tag, const std::vector<std::uint8_t>& body)
{
    std::vector<std::uint8_t> out(6u + body.size(), 0u);
    std::size_t written = 0u;
    const Status status = flynes::session::wire::encode_app_frame(
        tag, body.empty() ? nullptr : body.data(), body.size(), out.data(), out.size(), &written);
    expect(status == Status::Ok, "encode frame for tag " + std::to_string(tag));
    expect(written == out.size(), "encoded frame size");
    return out;
}

// Hand-built framing, so a negative golden whose body is already at the
// channel-independent maximum still gets a structurally valid frame. The
// encoder refuses such bodies on purpose; the parser must still reject them.
std::vector<std::uint8_t> raw_framed(std::uint16_t tag, const std::vector<std::uint8_t>& body)
{
    std::vector<std::uint8_t> out(6u + body.size(), 0u);
    const std::uint32_t declared = static_cast<std::uint32_t>(2u + body.size());
    out[0] = static_cast<std::uint8_t>((declared >> 24u) & 0xFFu);
    out[1] = static_cast<std::uint8_t>((declared >> 16u) & 0xFFu);
    out[2] = static_cast<std::uint8_t>((declared >> 8u) & 0xFFu);
    out[3] = static_cast<std::uint8_t>(declared & 0xFFu);
    out[4] = static_cast<std::uint8_t>(tag >> 8u);
    out[5] = static_cast<std::uint8_t>(tag & 0xFFu);
    std::memcpy(out.data() + 6u, body.data(), body.size());
    return out;
}

std::size_t index_of(const std::vector<std::string>& values, const std::string& value)
{
    const auto it = std::find(values.begin(), values.end(), value);
    if (it == values.end())
        return values.size();
    return static_cast<std::size_t>(it - values.begin());
}

void compare_environment_run(const std::string& what, Status naked, Status wrapped)
{
    expect(naked == wrapped, what + ": framed status " + status_name(wrapped) +
                                 " differs from naked check status " + status_name(naked));
}

} // namespace

int main()
{
    // ---- schema ----------------------------------------------------------
    const std::string schema = read_file(fs::path(FLYNES_SESSION_SCHEMA_FILE));
    expect(!schema.empty(), "schema readable");

    std::string kinds_body;
    expect(array_body(schema, "kinds", &kinds_body), "schema kinds array");
    std::vector<std::uint16_t> schema_kinds;
    for (const std::string& element : split_elements(kinds_body))
    {
        std::string kind_text;
        expect(top_level_string(element, "kind", &kind_text), "kind entry has a kind field");
        std::uint16_t kind = 0u;
        expect(parse_hex_kind(kind_text, &kind), "kind " + kind_text + " parses as hex");
        schema_kinds.push_back(kind);
    }
    expect(schema_kinds.size() == 62u,
           "schema declares 62 kinds, found " + std::to_string(schema_kinds.size()));

    std::string messages_body;
    expect(array_body(schema, "messages", &messages_body), "schema messages array");
    std::vector<std::string> schema_messages;
    for (const std::string& element : split_elements(messages_body))
    {
        std::string name;
        expect(top_level_string(element, "name", &name), "message entry has a name field");
        schema_messages.push_back(name);
    }
    expect(schema_messages.size() == 8u,
           "schema declares 8 messages, found " + std::to_string(schema_messages.size()));

    // ---- table: ObjectKind namespace exactly equals the schema kinds -------
    std::vector<std::uint16_t> table_kinds;
    std::vector<std::uint16_t> table_message_tags;
    for (std::size_t i = 0u; i < flynes::session::wire::frame_tag_count(); ++i)
    {
        std::uint16_t tag = 0u;
        FrameTypeNamespace name_space = FrameTypeNamespace::ObjectKind;
        const char* type_name = nullptr;
        expect(flynes::session::wire::frame_tag_entry(i, &tag, &name_space, &type_name),
               "frame tag entry " + std::to_string(i));
        expect(type_name != nullptr && type_name[0] != '\0', "frame tag entry has a type name");
        if (name_space == FrameTypeNamespace::ObjectKind)
        {
            expect(tag != 0u && tag <= 0xEFFFu, "ObjectKind tag inside 0x0001..0xEFFF");
            table_kinds.push_back(tag);
        }
        else
        {
            expect(tag >= flynes::session::wire::message_tag_base, "message tag inside the reserved range");
            table_message_tags.push_back(tag);
        }
    }
    expect(flynes::session::wire::frame_tag_count() == schema_kinds.size() + schema_messages.size(),
           "tag table size equals schema kinds plus schema messages");

    std::vector<std::uint16_t> sorted_table = table_kinds;
    std::vector<std::uint16_t> sorted_schema = schema_kinds;
    std::sort(sorted_table.begin(), sorted_table.end());
    std::sort(sorted_schema.begin(), sorted_schema.end());
    expect(sorted_table == sorted_schema,
           "ObjectKind tag table equals the schema kinds array exactly");
    for (std::size_t i = 0u; i < sorted_table.size(); ++i)
    {
        if (i >= sorted_schema.size() || sorted_table[i] != sorted_schema[i])
        {
            std::cerr << "  first disagreement at table index " << i << '\n';
            break;
        }
    }

    // Named type for a kind is the schema's hex kind spelling, which is exactly
    // what session_codec::check() dispatches on.
    for (std::uint16_t kind : schema_kinds)
    {
        char expected_name[8] = {};
        std::snprintf(expected_name, sizeof(expected_name), "0x%04x", static_cast<unsigned>(kind));
        const char* actual = flynes::session::wire::type_name_for_tag(kind);
        expect(actual != nullptr && std::strcmp(actual, expected_name) == 0,
               std::string("type name for kind ") + expected_name);
    }

    // ---- table: message namespace exactly equals the schema messages order --
    for (std::size_t i = 0u; i < schema_messages.size(); ++i)
    {
        const std::uint16_t tag =
            static_cast<std::uint16_t>(flynes::session::wire::message_tag_base + i);
        const char* type_name = flynes::session::wire::type_name_for_tag(tag);
        expect(type_name != nullptr && schema_messages[i] == type_name,
               "message tag for index " + std::to_string(i) + " is " + schema_messages[i]);
    }
    expect(table_message_tags.size() == schema_messages.size(), "message tag count");

    // The reserved tag range is a C2b design addition, not an approved design
    // value: keep it in one constant so a section 30 review can move it.
    expect(flynes::session::wire::message_tag_base == 0xFF00u, "message tag base constant");

    // ---- golden manifest --------------------------------------------------
    std::string manifest_body;
    expect(array_body(read_file(fs::path(FLYNES_SESSION_GOLDEN_DIR) / "manifest.json"), "cases",
                      &manifest_body),
           "golden manifest cases array");
    std::map<std::string, ManifestCase> cases;
    for (const std::string& element : split_elements(manifest_body))
    {
        ManifestCase entry;
        expect(top_level_string(element, "name", &entry.name), "manifest case has a name");
        entry.has_kind = top_level_string(element, "kind", &entry.kind);
        entry.has_message = top_level_string(element, "message", &entry.message);
        std::string length_text;
        expect(top_level_number(element, "length", &entry.length), "manifest case has a length");
        expect(!cases.count(entry.name), "manifest case names are unique: " + entry.name);
        cases[entry.name] = entry;
    }
    expect(!cases.empty(), "manifest has cases");

    // ---- golden corpus walk ----------------------------------------------
    const fs::path root(FLYNES_SESSION_GOLDEN_DIR);
    int directories = 0;
    int framed_ok = 0;
    int rejected_ok = 0;
    for (const auto& dir_entry : fs::directory_iterator(root))
    {
        if (!dir_entry.is_directory())
            continue;
        ++directories;
        const fs::path dir = dir_entry.path();
        const std::string name = dir.filename().string();
        const std::string type_name = read_text(dir / "type.txt");
        expect(!type_name.empty(), name + " type.txt");

        const auto it = cases.find(name);
        expect(it != cases.end(), name + " has a manifest case");
        if (it == cases.end())
            continue;
        const ManifestCase& entry = it->second;

        std::uint16_t tag = 0u;
        if (entry.has_kind)
        {
            expect(parse_hex_kind(entry.kind, &tag), name + " manifest kind parses");
            expect(entry.kind == type_name, name + " manifest kind equals type.txt");
        }
        else
        {
            expect(entry.has_message, name + " manifest carries a kind or a message");
            const std::size_t index = index_of(schema_messages, entry.message);
            expect(index < schema_messages.size(), name + " message is in the schema messages array");
            expect(entry.message == type_name, name + " manifest message equals type.txt");
            tag = static_cast<std::uint16_t>(flynes::session::wire::message_tag_base + index);
        }

        const char* table_name = flynes::session::wire::type_name_for_tag(tag);
        expect(table_name != nullptr && type_name == table_name,
               name + ": tag maps to " + type_name + " in the frame type table");

        std::vector<std::uint8_t> legal;
        expect(read_all(dir / "legal.bin", &legal), name + " legal.bin");
        expect(legal.size() == entry.length, name + " legal.bin size equals the manifest length");

        const bool negative_only = fs::exists(dir / "negative_only.txt");
        const std::vector<std::uint8_t> record = framed(tag, legal);
        expect(record.size() == 6u + legal.size(), name + " framed size");
        const std::size_t declared = (static_cast<std::size_t>(record[0]) << 24u) |
                                     (static_cast<std::size_t>(record[1]) << 16u) |
                                     (static_cast<std::size_t>(record[2]) << 8u) |
                                     static_cast<std::size_t>(record[3]);
        expect(declared == 2u + legal.size(), name + " frame_length counts the tag and the body");

        std::uint8_t naked_hash[32]{};
        const Status naked = flynes::session::wire::check(type_name.c_str(), legal.data(), legal.size(),
                                                          naked_hash);
        AppFrame parsed{};
        const Status identified =
            flynes::session::wire::identify_app_frame(record.data(), record.size(), &parsed);
        compare_environment_run(name + " legal.bin", naked, identified);

        if (negative_only)
        {
            expect(identified != Status::Ok, name + " negative-only legal.bin must fail closed");
        }
        else
        {
            expect(identified == Status::Ok, name + " framed legal.bin identifies");
            expect(type_name == parsed.type_name, name + " parsed type name");
            expect(parsed.frame_type_tag == tag, name + " parsed tag");
            expect(parsed.object_size == legal.size(), name + " parsed body size");
            expect(std::memcmp(parsed.object_bytes, legal.data(), legal.size()) == 0,
                   name + " parsed body bytes are the exact object bytes");
            expect(hex_of(parsed.object_hash, 32u) == read_text(dir / "legal.hash"),
                   name + " framed object hash equals the published golden hash");
            expect(!parsed.has_app_frame_hash,
                   name + " channel-independent identification does not claim a channel-bound hash");
            ++framed_ok;
        }

        // The per-channel allow-list must agree exactly with the parse result.
        std::vector<QuicChannel> legal_channels;
        for (std::uint8_t channel = 1u; channel <= 7u; ++channel)
        {
            const auto as_channel = static_cast<QuicChannel>(channel);
            if (flynes::session::wire::tag_is_legal_on(as_channel, tag))
                legal_channels.push_back(as_channel);
        }
        for (std::uint8_t channel = 1u; channel <= 7u; ++channel)
        {
            const auto as_channel = static_cast<QuicChannel>(channel);
            AppFrame candidate{};
            const Status status = flynes::session::wire::parse_app_frame(as_channel, record.data(),
                                                                        record.size(), &candidate);
            const bool expected_legal =
                std::find(legal_channels.begin(), legal_channels.end(), as_channel) !=
                legal_channels.end();
            if (!negative_only && expected_legal)
            {
                expect(status == Status::Ok, name + ": legal on channel " + std::to_string(channel));
                std::uint8_t classification[32]{};
                flynes::session::wire::compute_app_frame_hash(
                    as_channel, tag, legal.data(), legal.size(), classification);
                expect(candidate.has_app_frame_hash, name + " channel-bound parse has a class hash");
                expect(std::memcmp(candidate.app_frame_hash, classification, 32u) == 0,
                       name + ": classification hash matches the documented preimage");
                ++rejected_ok;
            }
            else
            {
                expect(status != Status::Ok,
                       name + ": rejected on channel " + std::to_string(channel));
            }
        }

        // Negative goldens stay failing closed when framed, and framing must
        // never change a validator verdict.
        const char* negatives[] = {"truncate.bin", "trailing.bin", "nonzero_reserved.bin",
                                   "unknown_enum.bin"};
        const Status expected_negative[] = {Status::Truncated, Status::Trailing,
                                            Status::NonzeroReserved, Status::UnknownEnum};
        const char* skip_files[] = {nullptr, nullptr, "skip_reserved.txt", "skip_enum.txt"};
        for (std::size_t i = 0u; i < 4u; ++i)
        {
            std::vector<std::uint8_t> body;
            if (!read_all(dir / negatives[i], &body))
            {
                expect(false, name + " missing " + negatives[i]);
                continue;
            }
            std::uint8_t hash[32]{};
            const Status naked_negative =
                flynes::session::wire::check(type_name.c_str(), body.data(), body.size(), hash);
            const std::vector<std::uint8_t> negative_record = raw_framed(tag, body);
            AppFrame candidate{};
            const Status framed_negative = flynes::session::wire::identify_app_frame(
                negative_record.data(), negative_record.size(), &candidate);
            // The corpus marks the negatives its validator cannot express with
            // skip_enum.txt / skip_reserved.txt (those bodies validate even
            // naked: they are byte mutations of objects with no reserved or enum
            // field), and a negative_only.txt directory has no valid legal.bin.
            // For those, only the invariant is asserted: framing never changes a
            // validator verdict, so it can neither reject an accepted body nor
            // accept a rejected one.
            const bool inexpressible =
                negative_only || (skip_files[i] != nullptr && fs::exists(dir / skip_files[i]));
            const bool within_bound =
                body.size() <= flynes::session::wire::absolute_max_object_bytes();
            if (!within_bound)
            {
                // Above the channel-independent maximum the framer rejects before
                // any validator can run: BadLength instead of the validator's own
                // verdict, and still fail-closed. The canonical transition frame
                // (0x000b) sits exactly at the maximum, so its trailing.bin
                // mutation is the only corpus body in this case.
                expect(framed_negative == Status::BadLength,
                       name + " " + negatives[i] + " above the maximum is a framing error");
            }
            else
            {
                // Framing never changes a validator verdict, so it can neither
                // reject an accepted body nor accept a rejected one.
                compare_environment_run(name + " " + negatives[i], naked_negative, framed_negative);
                if (!inexpressible)
                {
                    expect(framed_negative != Status::Ok,
                           name + " " + negatives[i] + " fails closed when framed");
                    expect(framed_negative == expected_negative[i],
                           name + " " + negatives[i] + " is " + status_name(expected_negative[i]));
                }
            }
        }
    }

    expect(directories == static_cast<int>(cases.size()),
           "every golden directory has a manifest case (" + std::to_string(directories) + " dirs, " +
               std::to_string(cases.size()) + " cases)");
    expect(framed_ok >= 30, "most golden entries round-trip through the framer");
    expect(rejected_ok > 0, "at least one golden entry is legal on a populated channel");

    if (failures != 0)
    {
        std::cerr << "flynes_session_app_frame_consistency: FAIL (" << failures << " of " << checks_run
                  << " checks)\n";
        return 1;
    }
    std::cout << "flynes_session_app_frame_consistency: PASS (" << checks_run << " checks, "
              << directories << " golden dirs, " << schema_kinds.size() << " schema kinds, "
              << schema_messages.size() << " schema messages)\n";
    return 0;
}
