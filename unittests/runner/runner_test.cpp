// Runner contract: a runner owns one match and plays it to full time before its
// construction returns. It has no control surface, so the match is complete and
// readable as soon as the runner exists.
#include <catch2/catch_test_macros.hpp>

#include "football/env.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/runner.hpp"

using football::runner::SingleRunner;

namespace {

constexpr PitchModel kPitch = PitchModel{105.0f, 68.0f, 0.04f, 1.6f, 0.015f};
constexpr BallModel kBall = BallModel{0.43f, 0.11f, 0.02f};

}  // namespace

TEST_CASE("a runner plays its match to full time") {
  SingleRunner runner(kPitch, kBall);

  REQUIRE(runner.Environment().Ticks() == kFullTime);
  REQUIRE(runner.Environment().Phase() == MatchPhase::FullTime);
  REQUIRE(runner.Environment().Finished());
}

TEST_CASE("runners own independent matches") {
  SingleRunner first(kPitch, kBall);
  SingleRunner second(kPitch, kBall);

  REQUIRE(first.Environment().Ticks() == kFullTime);
  REQUIRE(second.Environment().Ticks() == kFullTime);
  REQUIRE(&first.Environment() != &second.Environment());
}
