#include <algorithm>
#include <limits>
#include <stdexcept>
#include <tuple>

#include <catch2/catch_test_macros.hpp>

#include "sim/runtime/clock.hpp"

namespace {
using namespace football::sim;

auto Snapshot(const MatchClock& clock) {
  return std::tuple(clock.now(), clock.RegulationTime(), clock.BallInPlayTime(),
                    clock.ExecutedTicks(), clock.IsHalfUnderway(), clock.IsBallInPlay());
}

TEST_CASE("MatchClock separates ceremony regulation dead-ball and effective time",
          "[sim][clock]") {
  MatchClock clock(TickSpan{10});
  REQUIRE(clock.now() == Tick{});
  REQUIRE(clock.ExecutedTicks() == 0);
  REQUIRE_FALSE(clock.IsHalfUnderway());
  REQUIRE_FALSE(clock.IsBallInPlay());
  REQUIRE_THROWS_AS(clock.StartBallInPlay(), std::logic_error);
  REQUIRE(clock.Advance(TickSpan{100}, MatchPhase::PreMatch) == TickSpan{});
  REQUIRE(clock.now() == Tick{100});
  REQUIRE(clock.RegulationTime() == TickSpan{});
  // Publishing FirstHalf alone, even advancing time, does not start regulation.
  REQUIRE(clock.Advance(TickSpan{10}, MatchPhase::FirstHalf) == TickSpan{});
  clock.BeginHalf(); clock.StartBallInPlay();
  REQUIRE(clock.Advance(TickSpan{3}, MatchPhase::FirstHalf) == TickSpan{3});
  REQUIRE(clock.RegulationTime() == TickSpan{3});
  REQUIRE(clock.BallInPlayTime() == TickSpan{3});
  clock.StopBallInPlay();
  REQUIRE(clock.IsHalfUnderway());
  REQUIRE(clock.Advance(TickSpan{4}, MatchPhase::FirstHalf) == TickSpan{4});
  REQUIRE(clock.RegulationTime() == TickSpan{7});
  REQUIRE(clock.BallInPlayTime() == TickSpan{3});
  clock.BeginHalf(); clock.StartBallInPlay(); // Later accepted restart is idempotent.
  REQUIRE(clock.Advance(TickSpan{1}, MatchPhase::FirstHalf) == TickSpan{1});
  REQUIRE(clock.RegulationTime() == TickSpan{8});
  REQUIRE(clock.BallInPlayTime() == TickSpan{4});
  clock.EndHalf();
  REQUIRE_FALSE(clock.IsHalfUnderway());
  REQUIRE_FALSE(clock.IsBallInPlay());
  REQUIRE(clock.Advance(TickSpan{100}, MatchPhase::SecondHalf) == TickSpan{});
  REQUIRE(clock.RegulationTime() == TickSpan{8});
  clock.BeginHalf(); clock.StartBallInPlay();
  REQUIRE(clock.Advance(TickSpan{12}, MatchPhase::SecondHalf) == TickSpan{12});
  REQUIRE(clock.RegulationTime() == TickSpan{20});
  REQUIRE(clock.BallInPlayTime() == TickSpan{16});
}

TEST_CASE("manual clock jumps clip this period without starting the next",
          "[sim][clock]") {
  MatchClock clock(TickSpan{10});
  clock.BeginHalf(); clock.StartBallInPlay();
  REQUIRE(clock.Advance(TickSpan{100}, MatchPhase::FirstHalf) == TickSpan{10});
  REQUIRE(clock.now() == Tick{100});
  REQUIRE(clock.RegulationTime() == TickSpan{10});
  REQUIRE(clock.BallInPlayTime() == TickSpan{10});
  REQUIRE(clock.Advance(TickSpan{3}, MatchPhase::FirstHalf) == TickSpan{});
  REQUIRE(clock.now() == Tick{103});
  REQUIRE(clock.RegulationTime() == TickSpan{10});
  // The referee owns phase/whistle decisions. The clock only clips until told to stop.
  REQUIRE(clock.IsHalfUnderway());
  clock.EndHalf();
  REQUIRE(clock.Advance(TickSpan{3}, MatchPhase::SecondHalf) == TickSpan{});
  clock.BeginHalf(); clock.StartBallInPlay();
  REQUIRE(clock.Advance(TickSpan{100}, MatchPhase::SecondHalf) == TickSpan{10});
  REQUIRE(clock.RegulationTime() == TickSpan{20});
  REQUIRE(clock.BallInPlayTime() == TickSpan{20});
  clock.EndHalf();
  const auto finished = Snapshot(clock);
  REQUIRE(clock.Advance(TickSpan{std::numeric_limits<std::uint64_t>::max()},
                        MatchPhase::Finished) == TickSpan{});
  clock.CountExecutedStep(MatchPhase::Finished);
  REQUIRE(Snapshot(clock) == finished);
}

TEST_CASE("executed steps count independently of timeline including the terminal whistle",
          "[sim][clock]") {
  MatchClock clock(TickSpan{1});
  clock.CountExecutedStep(MatchPhase::PreMatch);
  REQUIRE(clock.ExecutedTicks() == 1);
  REQUIRE(clock.now() == Tick{});
  clock.Advance(TickSpan{100}, MatchPhase::PreMatch);
  REQUIRE(clock.ExecutedTicks() == 1);
  clock.BeginHalf(); clock.StartBallInPlay();
  clock.CountExecutedStep(MatchPhase::FirstHalf);
  clock.Advance(TickSpan{1}, MatchPhase::FirstHalf);
  clock.CountExecutedStep(MatchPhase::FirstHalf); // Half-time transition tick.
  clock.EndHalf();
  clock.CountExecutedStep(MatchPhase::SecondHalf);
  clock.BeginHalf(); clock.StartBallInPlay();
  clock.Advance(TickSpan{1}, MatchPhase::SecondHalf);
  const auto before_whistle = clock.now();
  clock.CountExecutedStep(MatchPhase::SecondHalf); // Count before publishing Finished.
  clock.EndHalf();
  clock.Advance(TickSpan{1}, MatchPhase::Finished);
  clock.CountExecutedStep(MatchPhase::Finished);
  REQUIRE(clock.ExecutedTicks() == 5);
  REQUIRE(clock.now() == before_whistle);
}

TEST_CASE("clock arithmetic keeps full integer precision and commits atomically",
          "[sim][clock]") {
  const auto maximum = std::numeric_limits<std::uint64_t>::max();
  REQUIRE_THROWS_AS(MatchClock(TickSpan{}), std::invalid_argument);
  REQUIRE_THROWS_AS(MatchClock(TickSpan{maximum / 2 + 1}), std::invalid_argument);
  MatchClock clock(TickSpan{maximum / 2});
  clock.BeginHalf(); clock.StartBallInPlay();
  REQUIRE(clock.Advance(TickSpan{maximum / 2}, MatchPhase::FirstHalf) == TickSpan{maximum / 2});
  REQUIRE(clock.Advance(TickSpan{maximum / 2}, MatchPhase::SecondHalf) == TickSpan{maximum / 2});
  REQUIRE(clock.RegulationTime() == TickSpan{maximum - 1});
  REQUIRE(clock.BallInPlayTime() == TickSpan{maximum - 1});
  REQUIRE(clock.Advance(TickSpan{1}, MatchPhase::SecondHalf) == TickSpan{});
  REQUIRE(clock.now() == Tick{maximum});
  const auto before = Snapshot(clock);
  REQUIRE_THROWS_AS(clock.Advance(TickSpan{1}, MatchPhase::SecondHalf), std::overflow_error);
  REQUIRE(Snapshot(clock) == before);

  MatchClock precise(TickSpan{UINT64_C(1) << 40});
  precise.BeginHalf(); precise.StartBallInPlay();
  precise.Advance(TickSpan{UINT64_C(1) << 25}, MatchPhase::FirstHalf);
  precise.Advance(TickSpan{1}, MatchPhase::FirstHalf);
  REQUIRE(precise.RegulationTime() == TickSpan{(UINT64_C(1) << 25) + 1});
  REQUIRE(precise.BallInPlayTime() == precise.RegulationTime());
}

TEST_CASE("clock admissions match an independent scalar oracle over both halves",
          "[sim][clock]") {
  for (std::uint64_t half : {1, 10, 1000}) {
    MatchClock clock(TickSpan{half});
    std::uint64_t timeline = 0, regulation = 0, effective = 0;
    for (MatchPhase phase : {MatchPhase::FirstHalf, MatchPhase::SecondHalf}) {
      clock.BeginHalf();
      const auto limit = phase == MatchPhase::FirstHalf ? half : half * 2;
      for (std::uint64_t index = 0; index < 31; ++index) {
        const bool live = index % 3 != 0;
        if (live) clock.StartBallInPlay(); else clock.StopBallInPlay();
        const TickSpan delta{index * 7};
        const auto admitted = std::min(delta.value, limit - regulation);
        timeline += delta.value;
        regulation += admitted;
        if (live) effective += admitted;
        REQUIRE(clock.Advance(delta, phase) == TickSpan{admitted});
        REQUIRE(clock.now() == Tick{timeline});
        REQUIRE(clock.RegulationTime() == TickSpan{regulation});
        REQUIRE(clock.BallInPlayTime() == TickSpan{effective});
        REQUIRE(clock.BallInPlayTime() <= clock.RegulationTime());
      }
      clock.EndHalf();
    }
  }
}

}  // namespace
