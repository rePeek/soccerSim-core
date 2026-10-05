#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>
#include <vector>

#include "app/args.hpp"

namespace {

// Builds a mutable argv for the parser, mirroring the real process layout
// (argv[0] is the program name and is never interpreted as a flag).
int ParseSteps(const std::vector<std::string>& flags) {
  std::vector<std::string> arguments{"football_app"};
  arguments.insert(arguments.end(), flags.begin(), flags.end());
  std::vector<char*> argv;
  argv.reserve(arguments.size());
  for (std::string& argument : arguments) argv.push_back(argument.data());
  return football::app::ParseSteps(static_cast<int>(argv.size()), argv.data());
}

}  // namespace

TEST_CASE("no arguments means zero ticks", "[app][args]") {
  REQUIRE(ParseSteps({}) == 0);
}

// A leading "--" in the test name would be parsed as a runner option by
// catch_discover_tests, so the flag is named in the middle of the sentence.
TEST_CASE("the --steps flag carries the tick count", "[app][args]") {
  REQUIRE(ParseSteps({"--steps=0"}) == 0);
  REQUIRE(ParseSteps({"--steps=1"}) == 1);
  REQUIRE(ParseSteps({"--steps=100"}) == 100);
  REQUIRE(ParseSteps({"--steps=2147483647"}) == 2147483647);
}

TEST_CASE("a later --steps overrides an earlier one", "[app][args]") {
  REQUIRE(ParseSteps({"--steps=10", "--steps=7"}) == 7);
}

TEST_CASE("an unknown flag is rejected", "[app][args]") {
  REQUIRE_THROWS_AS(ParseSteps({"--fps=60"}), std::runtime_error);
  REQUIRE_THROWS_AS(ParseSteps({"steps=10"}), std::runtime_error);
  REQUIRE_THROWS_AS(ParseSteps({"-s10"}), std::runtime_error);
  REQUIRE_THROWS_AS(ParseSteps({"--steps"}), std::runtime_error);
}

TEST_CASE("a malformed tick count is rejected", "[app][args]") {
  REQUIRE_THROWS_AS(ParseSteps({"--steps="}), std::runtime_error);
  REQUIRE_THROWS_AS(ParseSteps({"--steps=abc"}), std::runtime_error);
  REQUIRE_THROWS_AS(ParseSteps({"--steps=12abc"}), std::runtime_error);
  REQUIRE_THROWS_AS(ParseSteps({"--steps=1.5"}), std::runtime_error);
  REQUIRE_THROWS_AS(ParseSteps({"--steps=-1"}), std::runtime_error);
  REQUIRE_THROWS_AS(ParseSteps({"--fps=60", "--steps=10"}), std::runtime_error);
}

TEST_CASE("a tick count that overflows int does not silently wrap",
          "[app][args]") {
  REQUIRE_THROWS_AS(ParseSteps({"--steps=99999999999999999999"}),
                    std::runtime_error);
}
