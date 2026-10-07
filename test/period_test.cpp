#include <limits>

#include <catch2/catch_test_macros.hpp>

#include "sim/rules/period.hpp"

namespace {
using namespace football::sim;
using rules::PeriodElapsed;

TEST_CASE("period elapsed distinguishes underway halves from ceremonies", "[rules][period]") {
  for (const auto phase : {MatchPhase::PreMatch, MatchPhase::FirstHalf,
                           MatchPhase::SecondHalf, MatchPhase::Finished}) {
    for (const auto regulation : {TickSpan{}, TickSpan{1}, Seconds(10), Seconds(20),
                                  TickSpan{std::numeric_limits<std::uint64_t>::max()}}) {
      REQUIRE_FALSE(PeriodElapsed(false, phase, regulation, Seconds(10)));
    }
  }
}

TEST_CASE("period boundary is inclusive and uses cumulative regulation ticks", "[rules][period]") {
  for (const auto half : {TickSpan{1}, TickSpan{180}, Minutes(45),
                          TickSpan{std::numeric_limits<std::uint64_t>::max() / 2}}) {
    for (const auto phase : {MatchPhase::FirstHalf, MatchPhase::SecondHalf}) {
      const auto limit = phase == MatchPhase::SecondHalf ? half + half : half;
      REQUIRE_FALSE(PeriodElapsed(true, phase, limit - TickSpan{1}, half));
      REQUIRE(PeriodElapsed(true, phase, limit, half));
      REQUIRE(PeriodElapsed(true, phase, limit + TickSpan{1}, half));
    }
    REQUIRE_FALSE(PeriodElapsed(true, MatchPhase::SecondHalf, half, half));
  }
}

TEST_CASE("period predicate preserves the legacy phase branch without phase policy", "[rules][period]") {
  // These combinations are not produced by the runtime. Keep the old predicate:
  // only SecondHalf doubles the threshold; Process owns terminal freeze separately.
  for (const auto phase : {MatchPhase::PreMatch, MatchPhase::Finished}) {
    REQUIRE_FALSE(PeriodElapsed(true, phase, Seconds(10) - TickSpan{1}, Seconds(10)));
    REQUIRE(PeriodElapsed(true, phase, Seconds(10), Seconds(10)));
  }
}

TEST_CASE("period facts are explicit independent inputs and are not modified", "[rules][period]") {
  bool underway = true;
  auto phase = MatchPhase::FirstHalf;
  auto regulation = Seconds(10);
  auto duration = Seconds(10);
  REQUIRE(PeriodElapsed(underway, phase, regulation, duration));
  REQUIRE(underway);
  REQUIRE(phase == MatchPhase::FirstHalf);
  REQUIRE(regulation == Seconds(10));
  REQUIRE(duration == Seconds(10));
  phase = MatchPhase::SecondHalf;
  REQUIRE_FALSE(PeriodElapsed(underway, phase, regulation, duration));
  duration = Seconds(5);
  REQUIRE(PeriodElapsed(underway, phase, regulation, duration));
  underway = false;
  REQUIRE_FALSE(PeriodElapsed(underway, phase, regulation, duration));
}
}  // namespace
