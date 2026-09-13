#include "pair_capability.hpp"
#include "sha256.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <string>
#include <vector>

using flynes::session::wire::BearerPlanBytes;
using flynes::session::wire::PairSelectionStatus;
using flynes::session::wire::Status;
using flynes::session::wire::select_pair_plan;
using flynes::session::wire::validate_pair_capability;

namespace {

using Summary = std::array<std::uint8_t, 512>;
int failures = 0;

void check(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

BearerPlanBytes plan(std::uint8_t bearer = 2)
{
    BearerPlanBytes p{};
    p[0] = bearer;
    p[1] = 1;
    p[2] = 1;
    p[3] = bearer == 4 ? 1 : 2;
    p[4] = 1;
    p[6] = bearer == 2 ? 10 : (bearer == 3 ? 20 : 30);
    p[11] = 1;
    std::fill(p.begin() + 12, p.begin() + 44, std::uint8_t{1});
    return p;
}

Summary summary(std::vector<BearerPlanBytes> plans = {}, std::uint8_t platform = 1)
{
    Summary s{};
    s[1] = 1;
    s[8] = platform;
    s[9] = static_cast<std::uint8_t>(plans.size());
    s[32] = 1;
    s[64] = 1;
    std::sort(plans.begin(), plans.end());
    for (std::size_t i = 0; i < plans.size(); ++i)
        std::copy(plans[i].begin(), plans[i].end(), s.data() + 96 + i * 48);
    return s;
}

void valid(const Summary& s, const std::string& label)
{
    check(validate_pair_capability(s.data(), s.size()) == Status::Ok, label);
}

void invalid(const Summary& s, const std::string& label)
{
    check(validate_pair_capability(s.data(), s.size()) != Status::Ok, label);
}

void expect_selection(const Summary& a, const Summary& b,
                      PairSelectionStatus expected, const BearerPlanBytes& expected_plan = {})
{
    BearerPlanBytes selected;
    selected.fill(0xff);
    const auto a_before = a;
    const auto b_before = b;
    check(select_pair_plan(a.data(), a.size(), b.data(), b.size(), selected) == expected,
          "selection status");
    check(selected == expected_plan, "selected bytes or zero on failure");
    check(a == a_before && b == b_before, "selection never mutates input summaries");
}

void test_validation()
{
    valid(summary(), "zero plans is valid");
    std::vector<BearerPlanBytes> eight;
    for (std::uint8_t i = 1; i <= 8; ++i)
    {
        auto p = plan();
        p[11] = i;
        eight.push_back(p);
    }
    valid(summary(eight), "eight plans is valid");
    auto nine = summary(eight);
    nine[9] = 9;
    invalid(nine, "nine plans rejected before reading entries");

    const auto base = summary({plan()});
    check(validate_pair_capability(base.data(), 511) == Status::Truncated, "511 bytes");
    std::array<std::uint8_t, 513> long_summary{};
    std::copy(base.begin(), base.end(), long_summary.begin());
    check(validate_pair_capability(long_summary.data(), 513) == Status::Trailing, "513 bytes");
    for (const std::size_t size : {0u, 511u, 512u, 513u})
        check(validate_pair_capability(nullptr, size) != Status::Ok, "null summary");

    for (std::uint8_t platform = 1; platform <= 3; ++platform)
    {
        valid(summary({plan(2), plan(3), plan(4)}, platform), "all standard platform profiles");
        auto opaque = summary({}, platform);
        std::fill(opaque.begin() + 12, opaque.begin() + 16, std::uint8_t{0xff});
        valid(opaque, "os api level is opaque unsigned data");
    }
    auto apple = plan(3);
    apple[4] = 3;
    valid(summary({apple}, 3), "Apple endpoint on iOS native P2P");
    invalid(summary({apple}, 1), "Apple endpoint on Android rejected");
    invalid(summary({apple}, 2), "Apple endpoint on Harmony rejected");
    for (const auto bearer : std::initializer_list<std::uint8_t>{2, 4})
    {
        auto p = plan(bearer);
        p[4] = 3;
        invalid(summary({p}, 3), "Apple endpoint requires native P2P");
    }

    for (std::size_t offset = 0; offset < base.size(); ++offset)
    {
        const bool reserved = (offset >= 2 && offset < 8) ||
            (offset >= 10 && offset < 12) || (offset >= 16 && offset < 32) ||
            offset == 103 || (offset >= 140 && offset < 144) || offset >= 480;
        if (reserved)
        {
            auto s = base;
            s[offset] = 1;
            check(validate_pair_capability(s.data(), s.size()) == Status::NonzeroReserved,
                  "reserved byte " + std::to_string(offset));
        }
    }
    for (std::size_t offset = 144; offset < 480; ++offset)
    {
        auto s = base;
        s[offset] = 1;
        invalid(s, "unused entry garbage " + std::to_string(offset));
    }
    for (const std::size_t offset : {0u, 1u, 8u})
    {
        for (const auto value : std::initializer_list<std::uint8_t>{0, 4, 255})
        {
            if (base[offset] == value)
                continue;
            auto s = base;
            s[offset] = value;
            invalid(s, "invalid version/platform");
        }
    }
    for (const std::size_t offset : {32u, 64u})
    {
        auto s = base;
        std::fill(s.begin() + offset, s.begin() + offset + 32, std::uint8_t{0});
        invalid(s, "zero summary hash");
        s[offset + 31] = 1;
        valid(s, "summary hash nonzero only at final byte");
    }
    for (const std::size_t offset : {0u, 1u, 2u, 3u, 4u, 5u, 6u})
    {
        for (unsigned value = 0; value <= 255; ++value)
        {
            auto p = plan();
            p[offset] = static_cast<std::uint8_t>(value);
            const bool allowed = offset == 0 ? value == 2 :
                (offset == 1 || offset == 2 || offset == 4) ? (value == 1 || value == 2) :
                offset == 3 ? value == 2 : offset == 5 ? value <= 2 : value == 10;
            const auto s = summary({p});
            check((validate_pair_capability(s.data(), s.size()) == Status::Ok) == allowed,
                  "plan field " + std::to_string(offset) + " value " + std::to_string(value));
        }
    }
    for (const auto bearer : std::initializer_list<std::uint8_t>{2, 3, 4})
    {
        auto p = plan(bearer);
        p[3] = p[3] == 1 ? 2 : 1;
        invalid(summary({p}), "codec incompatible with bearer");
        p = plan(bearer);
        p[6] = 0;
        invalid(summary({p}), "rank fixed by bearer");
    }
    auto p = plan();
    p[11] = 0;
    invalid(summary({p}), "zero cert profile");
    for (std::size_t offset = 8; offset < 12; ++offset)
    {
        auto nonzero = p;
        nonzero[offset] = 1;
        valid(summary({nonzero}), "all cert profile bytes count");
    }
    p = plan();
    std::fill(p.begin() + 12, p.begin() + 44, std::uint8_t{0});
    invalid(summary({p}), "zero adapter hash");
    for (std::size_t offset = 12; offset < 44; ++offset)
    {
        auto nonzero = p;
        nonzero[offset] = 1;
        valid(summary({nonzero}), "all adapter hash bytes count");
    }
    invalid(summary({plan(), plan()}), "duplicate plans");
    auto descending = summary({plan(2), plan(3)});
    std::swap_ranges(descending.begin() + 96, descending.begin() + 144, descending.begin() + 144);
    invalid(descending, "descending plans");
}

void test_selection()
{
    const auto p = plan();
    const auto a = summary({p});
    expect_selection(a, a, PairSelectionStatus::Ok, p);
    expect_selection(summary(), a, PairSelectionStatus::NoCommonPlan);
    expect_selection(a, summary(), PairSelectionStatus::NoCommonPlan);
    expect_selection(summary(), summary(), PairSelectionStatus::NoCommonPlan);
    expect_selection(a, summary({plan(3), plan(4)}), PairSelectionStatus::NoCommonPlan);
    expect_selection(summary({p, plan(3), plan(4)}), summary({plan(3), plan(4)}),
                     PairSelectionStatus::Ok, plan(3));
    std::vector<BearerPlanBytes> eight;
    for (std::uint8_t i = 1; i <= 8; ++i)
    {
        auto entry = p;
        entry[11] = i;
        eight.push_back(entry);
    }
    expect_selection(summary(eight), summary({eight.back()}),
                     PairSelectionStatus::Ok, eight.back());
    expect_selection(summary({eight.back()}), summary(eight),
                     PairSelectionStatus::Ok, eight.back());

    auto different_database = a;
    different_database[32] = 2;
    different_database[64] = 3;
    expect_selection(a, different_database, PairSelectionStatus::Ok, p);
    expect_selection(a, summary({p}, 2), PairSelectionStatus::Ok, p);

    for (const std::size_t offset : {1u, 2u, 4u, 5u, 8u, 11u, 12u, 43u})
    {
        auto changed = p;
        ++changed[offset];
        expect_selection(a, summary({changed}), PairSelectionStatus::NoCommonPlan);
    }
    auto bad = a;
    bad[511] = 1;
    expect_selection(bad, a, PairSelectionStatus::InvalidInitiator);
    expect_selection(a, bad, PairSelectionStatus::InvalidResponder);
    expect_selection(bad, bad, PairSelectionStatus::InvalidInitiator);
    expect_selection(summary(), bad, PairSelectionStatus::InvalidResponder);
    auto late_bad = summary({p, plan(3)});
    late_bad[144 + 7] = 1;
    expect_selection(a, late_bad, PairSelectionStatus::InvalidResponder);
    for (const std::size_t size : {0u, 511u, 513u})
    {
        BearerPlanBytes out;
        out.fill(0xff);
        check(select_pair_plan(a.data(), size, a.data(), a.size(), out) ==
              PairSelectionStatus::InvalidInitiator && out == BearerPlanBytes{}, "bad initiator length clears");
        out.fill(0xff);
        check(select_pair_plan(a.data(), a.size(), a.data(), size, out) ==
              PairSelectionStatus::InvalidResponder && out == BearerPlanBytes{}, "bad responder length clears");
    }
    BearerPlanBytes out;
    out.fill(0xff);
    check(select_pair_plan(nullptr, 512, a.data(), a.size(), out) ==
          PairSelectionStatus::InvalidInitiator && out == BearerPlanBytes{}, "null initiator clears");
    out.fill(0xff);
    check(select_pair_plan(a.data(), a.size(), nullptr, 512, out) ==
          PairSelectionStatus::InvalidResponder && out == BearerPlanBytes{}, "null responder clears");

    // Rank uniquely fixes bearer and codec in valid v1 plans, so neither can
    // independently break a rank tie. Cover those profiles through rank order.
    auto aware = plan(2);
    aware[5] = 2;
    expect_selection(summary({aware, plan(3), plan(4)}), summary({plan(4), aware, plan(3)}),
                     PairSelectionStatus::Ok, aware);
    expect_selection(summary({plan(4), plan(3)}), summary({plan(4), plan(3)}),
                     PairSelectionStatus::Ok, plan(3));

    std::vector<std::size_t> independent_keys{5, 1, 2, 4};
    for (std::size_t offset = 8; offset < 44; ++offset)
        independent_keys.push_back(offset);
    for (const auto offset : independent_keys)
    {
        auto lower = p;
        auto higher = p;
        ++higher[offset];
        // Oppose later key values where possible to prove precedence.
        if (offset == 5) lower[1] = 2;
        if (offset == 1) lower[2] = 2;
        if (offset == 2) lower[4] = 2;
        if (offset == 4) lower[8] = 2;
        if (offset >= 8 && offset < 43) lower[offset + 1] = 0xff;
        const auto first = summary({higher, lower});
        auto second = summary({lower, higher});
        second[32] = 9;
        for (int repeat = 0; repeat < 3; ++repeat)
        {
            expect_selection(first, second, PairSelectionStatus::Ok, lower);
            expect_selection(second, first, PairSelectionStatus::Ok, lower);
        }
    }
}

void test_selected_hash()
{
    const auto s = summary({plan(3), plan(2), plan(4)});
    BearerPlanBytes selected{};
    check(select_pair_plan(s.data(), s.size(), s.data(), s.size(), selected) ==
          PairSelectionStatus::Ok, "hash selection");
    const auto digest = flynes::session::wire::domain_hash(
        "flynes-selected-bearer-plan-v1", selected.data(), selected.size());
    // Independent Python hashlib oracle: SHA256(domain || u32be(48) || entry).
    // Entry hex: 0201010201000a0000000001 + 32 bytes of 01 + 00000000.
    const std::array<std::uint8_t, 32> expected{
        0xeb, 0xce, 0x62, 0xbb, 0xf6, 0x73, 0xda, 0x40,
        0x52, 0xa6, 0xad, 0x92, 0x1a, 0x4d, 0x37, 0xca,
        0xd3, 0x8a, 0x90, 0xbf, 0x17, 0xcf, 0x57, 0x60,
        0xf0, 0xd7, 0xf9, 0x93, 0x1d, 0x4c, 0x07, 0xfe};
    check(digest == expected, "selected plan domain hash matches independent literal");
}

} // namespace

int main()
{
    test_validation();
    test_selection();
    test_selected_hash();
    if (failures != 0)
        return 1;
    std::cout << "pair capability validation and selection passed\n";
    return 0;
}
