#include <catch2/catch_test_macros.hpp>

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

namespace {

constexpr PitchModel kPitch{.length = 105.0f, .width = 68.0f,
                            .quadratic_resistance = 0.04f,
                            .ground_deceleration = 1.6f, .air_drag = 0.015f};
constexpr BallModel kBall{.mass = 0.43f, .radius = 0.11f,
                          .magnus_coefficient = 0.02f};

}

TEST_CASE("a runner plays its match to full time") {
  SingleRunner runner(kPitch, kBall);
  runner.Wait();

  REQUIRE(runner.Environment().Ticks() == kFullTime);
  REQUIRE(runner.Environment().Phase() == MatchPhase::FullTime);
  REQUIRE(runner.Environment().Finished());

  const auto& frames = runner.Environment().history().chunk().frames;
  REQUIRE(frames.size() == kFullTime.value + 1);
  REQUIRE(frames.front().tick == Tick{0});
  REQUIRE(frames.back().tick == kFullTime);
}

TEST_CASE("waiting twice is harmless") {
  SingleRunner runner(kPitch, kBall, Recording::Disabled);
  runner.Wait();
  runner.Wait();

  REQUIRE(runner.Environment().Ticks() == kFullTime);
}

TEST_CASE("stopping reaps the worker and keeps the match readable") {
  SingleRunner runner(kPitch, kBall, Recording::Disabled);
  runner.Stop();
  runner.Stop();

  const auto stopped_at = runner.Environment().Ticks();
  runner.Wait();
  REQUIRE(runner.Environment().Ticks() == stopped_at);
}

TEST_CASE("runners own independent matches") {
  SingleRunner first(kPitch, kBall, Recording::Disabled);
  SingleRunner second(kPitch, kBall, Recording::Disabled);

  first.Wait();
  second.Wait();

  REQUIRE(first.Environment().Ticks() == kFullTime);
  REQUIRE(second.Environment().Ticks() == kFullTime);
  REQUIRE(&first.Environment() != &second.Environment());
}

TEST_CASE("a runner can play without recording") {
  SingleRunner runner(kPitch, kBall, Recording::Disabled);
  runner.Wait();

  REQUIRE_FALSE(runner.Environment().history().enabled());
  REQUIRE(runner.Environment().history().chunk().frames.empty());
  REQUIRE(runner.Environment().Ticks() == kFullTime);
}
