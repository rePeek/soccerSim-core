// Runner contract: one runner owns one match and one worker. Playing and pausing
// belong to the runner, time and phase belong to the match — so progress is read
// from the match, which is safe after Pause() or once the loop has ended.
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <thread>

#include "football/env.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/runner.hpp"

using football::runner::SingleRunner;
using football::time::Tick;
using football::time::TickSpan;

namespace {

constexpr PitchModel kPitch = PitchModel{105.0f, 68.0f, 0.04f, 1.6f, 0.015f};
constexpr BallModel kBall = BallModel{0.43f, 0.11f, 0.02f};

// Wait until the predicate holds, or fail after a generous wall-clock bound.
template <typename Predicate>
bool WaitFor(Predicate done,
             std::chrono::milliseconds limit = std::chrono::seconds(10)) {
  const auto deadline = std::chrono::steady_clock::now() + limit;
  while (std::chrono::steady_clock::now() < deadline) {
    if (done()) return true;
    std::this_thread::sleep_for(std::chrono::microseconds(200));
  }
  return done();
}

// Waits for the worker to end the match, then reads the match it left behind.
std::uint64_t TicksWhenDone(SingleRunner& runner) {
  const bool ended = WaitFor([&runner] { return !runner.IsRunning(); });
  REQUIRE(ended);
  return runner.Environment().Ticks().value;
}

}  // namespace

TEST_CASE("a new runner is paused") {
  SingleRunner runner(kPitch, kBall);

  REQUIRE_FALSE(runner.IsRunning());
  REQUIRE(runner.Environment().Ticks() == Tick{0});

  // A paused runner never advances the match on its own.
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  REQUIRE(runner.Environment().Ticks() == Tick{0});
}

TEST_CASE("resume runs the match to full time") {
  SingleRunner runner(kPitch, kBall);

  runner.Resume();
  REQUIRE(TicksWhenDone(runner) == kFullTime.value);

  REQUIRE(runner.Environment().Finished());
  REQUIRE(runner.Environment().Phase() == MatchPhase::FullTime);
  REQUIRE_FALSE(runner.IsRunning());
}

TEST_CASE("pause parks the worker before it returns") {
  SingleRunner runner(kPitch, kBall);

  runner.Resume();
  REQUIRE(WaitFor([&runner] { return runner.IsRunning(); }));

  runner.Pause();
  runner.Pause();  // idempotent
  REQUIRE_FALSE(runner.IsRunning());

  // Pause() only returns once the worker is parked, so the tick cannot move.
  const std::uint64_t paused_at = runner.Environment().Ticks().value;
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  REQUIRE(runner.Environment().Ticks().value == paused_at);
}

TEST_CASE("resume continues from where the pause left off") {
  SingleRunner runner(kPitch, kBall);

  runner.Resume();
  REQUIRE(WaitFor([&runner] { return runner.Environment().Ticks() > Tick{0}; }));

  runner.Pause();
  const std::uint64_t paused_at = runner.Environment().Ticks().value;

  runner.Resume();
  REQUIRE(WaitFor([&runner, paused_at] {
    return runner.Environment().Ticks().value > paused_at;
  }));
  REQUIRE_FALSE(runner.Environment().Finished());
}

TEST_CASE("resuming a finished match does nothing") {
  SingleRunner runner(kPitch, kBall);

  runner.Resume();
  REQUIRE(TicksWhenDone(runner) == kFullTime.value);

  runner.Resume();
  REQUIRE_FALSE(runner.IsRunning());
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  REQUIRE(runner.Environment().Ticks() == kFullTime);
}

TEST_CASE("destroying a running runner joins its worker") {
  Env* match = nullptr;
  {
    SingleRunner runner(kPitch, kBall);
    runner.Resume();
    REQUIRE(WaitFor([&runner] { return runner.IsRunning(); }));
    match = &runner.Environment();
  }  // must stop the worker and join, not hang
  REQUIRE(match->Ticks() < kFullTime);  // the match was left behind intact
}

TEST_CASE("runners own independent matches") {
  SingleRunner first(kPitch, kBall);
  SingleRunner second(kPitch, kBall);

  first.Resume();
  REQUIRE(WaitFor([&first] { return first.IsRunning(); }));
  first.Pause();
  const std::uint64_t frozen = first.Environment().Ticks().value;

  second.Resume();
  REQUIRE(TicksWhenDone(second) == kFullTime.value);

  REQUIRE(first.Environment().Ticks().value == frozen);
  REQUIRE_FALSE(first.Environment().Finished());
}
