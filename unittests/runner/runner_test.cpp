#include <catch2/catch_test_macros.hpp>

#include "football/env.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/runner.hpp"

using football::ball::BallModel;
using football::pitch::PitchModel;
using football::kFullTime;
using football::MatchPhase;
using football::runner::SingleRunner;
using football::time::Tick;

namespace {

constexpr PitchModel kPitch = PitchModel{105.0f, 68.0f, 0.04f, 1.6f, 0.015f};
constexpr BallModel kBall = BallModel{0.43f, 0.11f, 0.02f};

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
  SingleRunner runner(kPitch, kBall, false);
  runner.Wait();
  runner.Wait();

  REQUIRE(runner.Environment().Ticks() == kFullTime);
}

TEST_CASE("stopping reaps the worker and keeps the match readable") {
  SingleRunner runner(kPitch, kBall, false);
  runner.Stop();
  runner.Stop();

  const auto stopped_at = runner.Environment().Ticks();
  runner.Wait();
  REQUIRE(runner.Environment().Ticks() == stopped_at);
}

TEST_CASE("runners own independent matches") {
  SingleRunner first(kPitch, kBall, false);
  SingleRunner second(kPitch, kBall, false);

  first.Wait();
  second.Wait();

  REQUIRE(first.Environment().Ticks() == kFullTime);
  REQUIRE(second.Environment().Ticks() == kFullTime);
  REQUIRE(&first.Environment() != &second.Environment());
}

TEST_CASE("a runner can play without recording") {
  SingleRunner runner(kPitch, kBall, false);
  runner.Wait();

  REQUIRE_FALSE(runner.Environment().history().enabled());
  REQUIRE(runner.Environment().history().chunk().frames.empty());
  REQUIRE(runner.Environment().Ticks() == kFullTime);
}
