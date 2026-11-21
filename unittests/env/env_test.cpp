#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>

#include "football/env.hpp"
#include "football/foundation/time/tick.hpp"

using football::ball::BallModel;
using football::history::Recording;
using football::Env;
using football::kFullTime;
using football::kHalfTime;
using football::MatchPhase;
using football::PhaseOfTick;
using football::pitch::PitchModel;
using football::time::Tick;
using football::time::TickSpan;
using Catch::Approx;

namespace {

constexpr PitchModel kPitch{.length = 105.0f, .width = 68.0f,
                            .quadratic_resistance = 0.04f,
                            .ground_deceleration = 1.6f, .air_drag = 0.015f};
constexpr BallModel kBall{.mass = 0.43f, .radius = 0.11f,
                          .magnus_coefficient = 0.02f};

}

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

TEST_CASE("a new match records its opening frame") {
  Env env(kPitch, kBall);
  const auto opening = env.history().ReadSnapshot(Tick{0});
  REQUIRE(opening);
  REQUIRE(opening->tick == Tick{0});
  REQUIRE(opening->ball.position.coords[0] == Approx(0.0f));
  REQUIRE(opening->ball.position.coords[1] == Approx(0.0f));
  REQUIRE(opening->ball.position.coords[2] == Approx(kBall.radius));
}

TEST_CASE("every LoopMain tick records the tick it committed") {
  Env env(kPitch, kBall);
  env.LoopMain(TickSpan{1});
  env.LoopMain(TickSpan{1});
  REQUIRE(env.history().ReadSnapshot(Tick{2}));
  REQUIRE(env.history().ReadSnapshot(Tick{2})->tick == env.Ticks());
  REQUIRE_FALSE(env.history().ReadSnapshot(Tick{3}));
}

TEST_CASE("recorded frames are values, not views") {
  Env env(kPitch, kBall);
  env.LoopMain(TickSpan{1});
  const auto recorded = env.history().ReadSnapshot(Tick{1});
  REQUIRE(recorded);
  env.LoopMain(TickSpan{50});
  REQUIRE(env.history().ReadSnapshot(Tick{1}) == recorded);
}

TEST_CASE("a match can be built without recording") {
  Env env(kPitch, kBall, Recording::Disabled);
  env.LoopMain(TickSpan{10});
  REQUIRE_FALSE(env.history().enabled());
  REQUIRE_FALSE(env.history().ReadSnapshot(Tick{0}));
  REQUIRE(env.Ticks() == Tick{10});
}

TEST_CASE("equal inputs record equal frames") {
  Env first(kPitch, kBall);
  Env second(kPitch, kBall);
  for (int i = 0; i < 6; ++i) {
    first.LoopMain(TickSpan{100});
    second.LoopMain(TickSpan{100});
  }
  for (const Tick tick : {Tick{0}, Tick{1}, Tick{300}, Tick{600}}) {
    REQUIRE(first.history().ReadSnapshot(tick) ==
            second.history().ReadSnapshot(tick));
  }
}

TEST_CASE("LoopMain plays the whole match without a tick limit") {
  Env env(kPitch, kBall, Recording::Disabled);
  env.LoopMain(TickSpan{});
  REQUIRE(env.Ticks() == kFullTime);
  REQUIRE(env.Finished());
}

TEST_CASE("LoopMain stops once its tick limit is reached") {
  Env env(kPitch, kBall, Recording::Disabled);
  env.LoopMain(TickSpan{100});
  REQUIRE(env.Ticks() == Tick{100});
  REQUIRE_FALSE(env.Finished());
}

TEST_CASE("LoopMain stops at full time before a large tick limit") {
  Env env(kPitch, kBall, Recording::Disabled);
  env.LoopMain(TickSpan{kFullTime.value + 1000});
  REQUIRE(env.Ticks() == kFullTime);
  REQUIRE(env.Finished());
}

TEST_CASE("LoopMain advances exactly one tick") {
  Env env(kPitch, kBall);
  env.LoopMain(TickSpan{1});
  REQUIRE(env.Ticks() == Tick{1});
  env.LoopMain(TickSpan{1});
  REQUIRE(env.Ticks() == Tick{2});
}

TEST_CASE("advance runs exactly the requested ticks") {
  Env env(kPitch, kBall);
  env.LoopMain(TickSpan{500});
  REQUIRE(env.Ticks() == Tick{500});
  REQUIRE(env.Phase() == MatchPhase::FirstHalf);
  env.LoopMain(TickSpan{250});
  REQUIRE(env.Ticks() == Tick{750});
}

TEST_CASE("the phase boundaries come from the cursor") {
  Env env(kPitch, kBall);
  env.LoopMain(kHalfTime - Tick{1});
  REQUIRE(env.Ticks() == Tick{kHalfTime.value - 1});
  REQUIRE(env.Phase() == MatchPhase::FirstHalf);
  env.LoopMain(TickSpan{1});
  REQUIRE(env.Ticks() == kHalfTime);
  REQUIRE(env.Phase() == MatchPhase::HalfTime);
  REQUIRE_FALSE(env.Finished());
  env.LoopMain(TickSpan{1});
  REQUIRE(env.Phase() == MatchPhase::SecondHalf);
  REQUIRE_FALSE(env.Finished());
}

TEST_CASE("advancing past full time stops at the horizon") {
  Env env(kPitch, kBall);
  env.LoopMain(TickSpan{kFullTime.value + 1000});
  REQUIRE(env.Ticks() == kFullTime);
  REQUIRE(env.Finished());
  REQUIRE(env.Phase() == MatchPhase::FullTime);
  REQUIRE(env.history().ReadSnapshot(kFullTime));
  env.LoopMain(TickSpan{10});
  REQUIRE(env.Ticks() == kFullTime);
  REQUIRE_FALSE(env.history().ReadSnapshot(Tick{kFullTime.value + 1}));
}

TEST_CASE("environments are independent") {
  Env first(kPitch, kBall);
  Env second(kPitch, kBall);
  first.LoopMain(TickSpan{7});
  REQUIRE(first.Ticks() == Tick{7});
  REQUIRE(second.Ticks() == Tick{0});
  second.LoopMain(TickSpan{3});
  REQUIRE(second.Ticks() == Tick{3});
  REQUIRE(first.Ticks() == Tick{7});
  REQUIRE(first.Phase() == second.Phase());
}

TEST_CASE("equal inputs produce equal results") {
  Env first(kPitch, kBall);
  Env second(kPitch, kBall);
  for (int i = 0; i < 6; ++i) {
    first.LoopMain(TickSpan{100});
    second.LoopMain(TickSpan{100});
    REQUIRE(first.Ticks() == second.Ticks());
    REQUIRE(first.Phase() == second.Phase());
    REQUIRE(first.Finished() == second.Finished());
  }
  REQUIRE(first.Ticks() == Tick{600});
}

TEST_CASE("half time is exactly one tick wide") {
  Env env(kPitch, kBall);
  env.LoopMain(kHalfTime - Tick{1});
  REQUIRE(env.Phase() == MatchPhase::FirstHalf);
  env.LoopMain(TickSpan{1});
  REQUIRE(env.Phase() == MatchPhase::HalfTime);
  env.LoopMain(TickSpan{1});
  REQUIRE(env.Phase() == MatchPhase::SecondHalf);
  env.LoopMain(kFullTime - env.Ticks());
  REQUIRE(env.Phase() == MatchPhase::FullTime);
  REQUIRE(env.Finished());
}

TEST_CASE("the stored phase always agrees with the cursor") {
  Env env(kPitch, kBall);
  constexpr std::uint64_t kStride = 37;
  for (std::uint64_t tick = 0; tick <= 2000; tick += kStride) {
    REQUIRE(env.Ticks().value == tick);
    REQUIRE(env.Phase() == PhaseOfTick(tick));
    env.LoopMain(TickSpan{kStride});
  }
  env.LoopMain(kFullTime - env.Ticks());
  REQUIRE(env.Ticks() == kFullTime);
  REQUIRE(env.Phase() == PhaseOfTick(kFullTime.value));
  REQUIRE(env.Phase() == MatchPhase::FullTime);
}

TEST_CASE("finished follows the stored phase") {
  Env env(kPitch, kBall);
  REQUIRE_FALSE(env.Finished());
  env.LoopMain(kFullTime - Tick{1});
  REQUIRE(env.Phase() == MatchPhase::SecondHalf);
  REQUIRE_FALSE(env.Finished());
  env.LoopMain(TickSpan{1});
  REQUIRE(env.Phase() == MatchPhase::FullTime);
  REQUIRE(env.Finished());
}
