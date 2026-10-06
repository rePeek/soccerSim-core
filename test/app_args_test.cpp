#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "app/args.hpp"

namespace {
football::app::AppOptions Parse(const std::vector<std::string>& flags) {
  std::vector<std::string> arguments{"football_app"};
  arguments.insert(arguments.end(), flags.begin(), flags.end());
  std::vector<char*> argv;
  for (auto& argument : arguments) argv.push_back(argument.data());
  return football::app::ParseArgs(static_cast<int>(argv.size()), argv.data());
}
}

TEST_CASE("no arguments leave regulation duration to simulation", "[app][args]") {
  REQUIRE_FALSE(Parse({}).half_duration_ms);
}
TEST_CASE("the half-duration flag configures a complete match not a tick budget", "[app][args]") {
  REQUIRE(Parse({"--half-duration-ms=1"}).half_duration_ms == 1);
  REQUIRE(Parse({"--half-duration-ms=1800"}).half_duration_ms == 1800);
  REQUIRE(Parse({"--half-duration-ms=2700000"}).half_duration_ms == 2700000);
}
TEST_CASE("a later half-duration overrides an earlier one", "[app][args]") {
  REQUIRE(Parse({"--half-duration-ms=1800", "--half-duration-ms=3600"}).half_duration_ms == 3600);
}
TEST_CASE("retired tick budgets and unknown flags are rejected", "[app][args]") {
  for (const auto* flag : {"--steps=10", "--fps=60", "-s10", "--half-duration-ms"})
    REQUIRE_THROWS_AS(Parse({flag}), std::runtime_error);
}
TEST_CASE("malformed or non-positive regulation duration is rejected", "[app][args]") {
  for (const auto* value : {"", "abc", "12abc", "1.5", "-1", "0", "+1", " 1"})
    REQUIRE_THROWS_AS(Parse({std::string("--half-duration-ms=") + value}), std::runtime_error);
}
TEST_CASE("overflowing regulation duration does not silently wrap", "[app][args]") {
  REQUIRE_THROWS_AS(Parse({"--half-duration-ms=18446744073709551616"}), std::runtime_error);
}
