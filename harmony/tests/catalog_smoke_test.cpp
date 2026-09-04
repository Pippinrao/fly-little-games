#include "catalog_smoke.hpp"

#include <flynes/flynes_app.h>

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>

namespace {

int failures = 0;

void expect(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

template <typename Function>
std::string capture_error(Function&& function)
{
    try
    {
        function();
    }
    catch (const std::exception& error)
    {
        return error.what();
    }
    catch (...)
    {
        return "non-standard exception";
    }

    return {};
}

void test_lifecycle_smoke_returns_decimal_snapshot_values()
{
    const flynes::harmony::CatalogSmokeResult result =
        flynes::harmony::run_catalog_smoke("data", "cache");

    expect(result.generation == "0", "generation must be returned as a decimal string");
    expect(result.count == "0", "count must be returned as a decimal string");
}

void test_empty_root_is_rejected_before_create()
{
    const std::string message = capture_error([] {
        (void)flynes::harmony::run_catalog_smoke({}, "cache");
    });

    expect(message.find("validate data root") != std::string::npos,
           "empty data root must identify its validation step");
    expect(message.find("fly_result=") != std::string::npos,
           "validation failure must include the fly_result code");
}

void test_oversized_root_is_rejected_before_narrowing()
{
    const std::string oversized(FLY_APP_ROOT_MAX_UTF8_BYTES + 1u, 'x');
    const std::string message = capture_error([&oversized] {
        (void)flynes::harmony::run_catalog_smoke("data", oversized);
    });

    expect(message.find("validate cache root") != std::string::npos,
           "oversized cache root must identify its validation step");
    expect(message.find("fly_result=") != std::string::npos,
           "oversized root failure must include the fly_result code");
}

void test_create_failure_does_not_echo_root_bytes()
{
    const std::string sensitive_root("secret\0path", 11u);
    const std::string message = capture_error([&sensitive_root] {
        (void)flynes::harmony::run_catalog_smoke(sensitive_root, "cache");
    });

    expect(message.find("fly_app_create") != std::string::npos,
           "library rejection must identify fly_app_create");
    expect(message.find("fly_result=") != std::string::npos,
           "library rejection must include the fly_result code");
    expect(message.find("secret") == std::string::npos,
           "native errors must not echo sandbox path bytes");
}

} // namespace

int main()
{
    test_lifecycle_smoke_returns_decimal_snapshot_values();
    test_empty_root_is_rejected_before_create();
    test_oversized_root_is_rejected_before_narrowing();
    test_create_failure_does_not_echo_root_bytes();

    if (failures != 0)
    {
        std::cerr << failures << " failure(s)\n";
        return EXIT_FAILURE;
    }

    std::cout << "PASS: Harmony catalog smoke contract\n";
    return EXIT_SUCCESS;
}
