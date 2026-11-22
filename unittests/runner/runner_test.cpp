#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <type_traits>
#include <utility>

#include "football/env.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/runner.hpp"

using football::ball::BallModel;
using football::pitch::PitchModel;
using football::history::Recording;
using football::kFullTime;
using football::MatchPhase;
using football::runner::SingleRunner;
using football::time::Tick;

static_assert(std::is_same_v<decltype(std::declval<SingleRunner&>().EnvironmentAfterJoin()),
                             const football::Env&>);
static_assert(std::is_same_v<decltype(std::declval<const SingleRunner&>().history()),
                             const football::history::History&>);

namespace {

constexpr PitchModel kPitch{.length = 105.0f, .width = 68.0f,
                            .quadratic_resistance = 0.04f,
                            .ground_deceleration = 1.6f, .air_drag = 0.015f};
constexpr BallModel kBall{.mass = 0.43f, .radius = 0.11f,
                          .magnus_coefficient = 0.02f};

}

TEST_CASE("a runner exposes history but rejects live environment access before joining") {
  SingleRunner runner(kPitch, kBall);
  const SingleRunner& reader = runner;
  REQUIRE_THROWS_AS(reader.EnvironmentAfterJoin(), std::logic_error);
  const auto opening = reader.history().ReadSnapshot(Tick{0});
  REQUIRE(opening);
  REQUIRE(opening->tick == Tick{0});
  REQUIRE(opening->ball.position.coords[2] == kBall.radius);
  runner.Stop();
  REQUIRE(&reader.history() == &reader.EnvironmentAfterJoin().history());
}

TEST_CASE("a runner plays its match to full time") {
  SingleRunner runner(kPitch, kBall);
  runner.Wait();

  const auto& env = runner.EnvironmentAfterJoin();
  REQUIRE(env.Ticks() == kFullTime);
  REQUIRE(env.Phase() == MatchPhase::FullTime);
  REQUIRE(env.Finished());
  REQUIRE(runner.history().ReadSnapshot(Tick{0}));
  const auto final_frame = runner.history().ReadSnapshot(kFullTime);
  REQUIRE(final_frame);
  REQUIRE(final_frame->tick == kFullTime);
  REQUIRE_FALSE(runner.history().ReadSnapshot(Tick{kFullTime.value + 1}));
}

TEST_CASE("waiting twice is harmless") {
  SingleRunner runner(kPitch, kBall, Recording::Disabled);
  runner.Wait();
  runner.Wait();
  REQUIRE(runner.EnvironmentAfterJoin().Ticks() == kFullTime);
}

TEST_CASE("stopping reaps the worker and keeps the match readable") {
  SingleRunner runner(kPitch, kBall, Recording::Disabled);
  REQUIRE_THROWS_AS(runner.EnvironmentAfterJoin(), std::logic_error);
  runner.Stop();
  runner.Stop();
  const auto stopped_at = runner.EnvironmentAfterJoin().Ticks();
  runner.Wait();
  REQUIRE(runner.EnvironmentAfterJoin().Ticks() == stopped_at);
}

TEST_CASE("runners own independent matches") {
  SingleRunner first(kPitch, kBall, Recording::Disabled);
  SingleRunner second(kPitch, kBall, Recording::Disabled);
  first.Wait();
  second.Wait();
  REQUIRE(first.EnvironmentAfterJoin().Ticks() == kFullTime);
  REQUIRE(second.EnvironmentAfterJoin().Ticks() == kFullTime);
  REQUIRE(&first.EnvironmentAfterJoin() != &second.EnvironmentAfterJoin());
}

TEST_CASE("a runner can play without recording") {
  SingleRunner runner(kPitch, kBall, Recording::Disabled);
  REQUIRE_FALSE(runner.history().enabled());
  REQUIRE_FALSE(runner.history().ReadSnapshot(Tick{0}));
  runner.Wait();
  REQUIRE_FALSE(runner.history().ReadSnapshot(kFullTime));
  REQUIRE(runner.EnvironmentAfterJoin().Ticks() == kFullTime);
}
