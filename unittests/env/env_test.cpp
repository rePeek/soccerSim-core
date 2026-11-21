// Env contract: one match's state and rules, advanced only by explicit calls.
// Nothing here waits on a clock or a thread, so every assertion is exact.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "football/env.hpp"
#include "football/foundation/time/tick.hpp"

using football::ball::BallModel;
using football::pitch::PitchModel;
using football::Env;
using football::kFullTime;
using football::kHalfTime;
using football::MatchPhase;
using football::PhaseOfTick;
using football::time::Tick;
using football::time::TickSpan;
using Catch::Approx;

namespace {

constexpr PitchModel kPitch = PitchModel{105.0f, 68.0f, 0.04f, 1.6f, 0.015f};
constexpr BallModel kBall = BallModel{0.43f, 0.11f, 0.02f};

}  // namespace

TEST_CASE("a match is 45 and 90 minutes at 100 Hz") {
  REQUIRE(kHalfTime == Tick{270000});
  REQUIRE(kFullTime == Tick{540000});
  REQUIRE(football::time::ToSeconds(football::time::Minutes(45)) ==
          Catch::Approx(2700.0f));
  REQUIRE(football::time::ToSeconds(football::time::Minutes(90)) ==
          Catch::Approx(5400.0f));
}

TEST_CASE("a new environment is at tick 0 in the first half") {
  Env env(kPitch, kBall);

  REQUIRE(env.Ticks() == Tick{0});
  REQUIRE(env.Phase() == MatchPhase::FirstHalf);
  REQUIRE_FALSE(env.Finished());
}

TEST_CASE("step advances exactly one tick") {
  Env env(kPitch, kBall);

  env.Step();
  REQUIRE(env.Ticks() == Tick{1});
  env.Step();
  REQUIRE(env.Ticks() == Tick{2});
}

TEST_CASE("advance runs exactly the requested ticks") {
  Env env(kPitch, kBall);

  env.Step(TickSpan{500});
  REQUIRE(env.Ticks() == Tick{500});
  REQUIRE(env.Phase() == MatchPhase::FirstHalf);

  env.Step(TickSpan{0});
  REQUIRE(env.Ticks() == Tick{500});  // an empty advance is a no-op

  env.Step(TickSpan{250});
  REQUIRE(env.Ticks() == Tick{750});
}

TEST_CASE("the phase boundaries come from the cursor") {
  Env env(kPitch, kBall);

  env.Step(kHalfTime - Tick{1});
  REQUIRE(env.Ticks() == Tick{kHalfTime.value - 1});
  REQUIRE(env.Phase() == MatchPhase::FirstHalf);

  env.Step();  // tick 270000 is half time itself, one tick wide
  REQUIRE(env.Ticks() == kHalfTime);
  REQUIRE(env.Phase() == MatchPhase::HalfTime);
  REQUIRE_FALSE(env.Finished());

  env.Step();  // and the second half starts on the next tick
  REQUIRE(env.Phase() == MatchPhase::SecondHalf);
  REQUIRE_FALSE(env.Finished());
}

TEST_CASE("the bulk step stops at full time") {
  Env env(kPitch, kBall);

  // Asking for more than the match has left stops at the horizon.
  env.Step(TickSpan{kFullTime.value + 1000});
  REQUIRE(env.Ticks() == kFullTime);
  REQUIRE(env.Finished());
  REQUIRE(env.Phase() == MatchPhase::FullTime);

  // Another bulk step on a finished match is a no-op.
  env.Step(TickSpan{10});
  REQUIRE(env.Ticks() == kFullTime);

  // A single Step() is not guarded: stopping is the executor's job, so the
  // phase saturates at full time even if a caller keeps stepping.
  env.Step();
  REQUIRE(env.Ticks().value == kFullTime.value + 1);
  REQUIRE(env.Phase() == MatchPhase::FullTime);
  REQUIRE(env.Finished());
}

TEST_CASE("environments are independent") {
  Env first(kPitch, kBall);
  Env second(kPitch, kBall);

  first.Step(TickSpan{7});
  REQUIRE(first.Ticks() == Tick{7});
  REQUIRE(second.Ticks() == Tick{0});

  second.Step(TickSpan{3});
  REQUIRE(second.Ticks() == Tick{3});
  REQUIRE(first.Ticks() == Tick{7});
  REQUIRE(first.Phase() == second.Phase());
}

TEST_CASE("equal inputs produce equal results") {
  // No RNG and no shared state: two matches fed the same calls agree exactly.
  Env first(kPitch, kBall);
  Env second(kPitch, kBall);

  for (int i = 0; i < 6; ++i) {
    first.Step(TickSpan{100});
    second.Step(TickSpan{100});
    REQUIRE(first.Ticks() == second.Ticks());
    REQUIRE(first.Phase() == second.Phase());
    REQUIRE(first.Finished() == second.Finished());
  }
  REQUIRE(first.Ticks() == Tick{600});
}

TEST_CASE("half time is exactly one tick wide") {
  Env env(kPitch, kBall);

  env.Step(kHalfTime - Tick{1});
  REQUIRE(env.Phase() == MatchPhase::FirstHalf);

  env.Step();  // kHalfTime
  REQUIRE(env.Phase() == MatchPhase::HalfTime);

  env.Step();  // kHalfTime + 1
  REQUIRE(env.Phase() == MatchPhase::SecondHalf);

  // Full time keeps its own phase from kFullTime onwards.
  env.Step(kFullTime - env.Ticks());
  REQUIRE(env.Phase() == MatchPhase::FullTime);
  REQUIRE(env.Finished());
}

TEST_CASE("the stored phase always agrees with the cursor") {
  // The phase is cached state, so it must never drift from the tick it mirrors.
  Env env(kPitch, kBall);

  constexpr std::uint64_t kStride = 37;
  for (std::uint64_t tick = 0; tick <= 2000; tick += kStride) {
    REQUIRE(env.Ticks().value == tick);
    REQUIRE(env.Phase() == PhaseOfTick(tick));
    env.Step(TickSpan{kStride});
  }

  // Walk the last tick before full time and across it as well.
  env.Step(kFullTime - env.Ticks());
  REQUIRE(env.Ticks() == kFullTime);
  REQUIRE(env.Phase() == PhaseOfTick(kFullTime.value));
  REQUIRE(env.Phase() == MatchPhase::FullTime);
}

TEST_CASE("finished follows the stored phase") {
  Env env(kPitch, kBall);
  REQUIRE_FALSE(env.Finished());

  env.Step(kFullTime - Tick{1});
  REQUIRE(env.Phase() == MatchPhase::SecondHalf);
  REQUIRE_FALSE(env.Finished());

  env.Step();  // the tick that reaches full time flips the phase
  REQUIRE(env.Phase() == MatchPhase::FullTime);
  REQUIRE(env.Finished());
}
