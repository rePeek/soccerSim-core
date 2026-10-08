#include <limits>

#include <catch2/catch_test_macros.hpp>

#include "app/fixtures/default_teams.hpp"
#include "sim/testing/simulation_access.hpp"
#include "sim/observation/mentalimage.hpp"
#include "sim/simulation.hpp"

using football::ball::Ball;

namespace {
using namespace football::sim;
using football::sim::testing::SimulationAccess;

TEST_CASE("Ball samples and mental-image ages use ticks without changing quantization",
          "[sim][tick][prediction]") {
  for (bool reverse : {false, true}) {
    MatchOptions options;
    options.reverse_team_processing = reverse;
    Simulation simulation;
    simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
                    football::app::fixtures::MakeDefaultAwayTeam(),
                    football::model::MakeLegacyPitch(), options);
    Ball* ball = SimulationAccess::BallOf(simulation);
    ball->SetPosition(blunted::Vector3(0, 0, 10), SimulationAccess::BallEnvironmentOf(simulation));
    ball->SetMomentum(blunted::Vector3(8, 1, 3), SimulationAccess::BallEnvironmentOf(simulation));
    MentalImage image(SimulationAccess::NowOf(simulation), {}, *ball);
    REQUIRE(image.captured_tick == SimulationAccess::NowOf(simulation));
    REQUIRE(image.GetAge(SimulationAccess::NowOf(simulation)) == TickSpan{});
    REQUIRE(image.ballPredictions.size() == ball_timing::kPredictionHorizon.value);
    for (std::uint64_t ticks = 0; ticks < 400; ++ticks) {
      REQUIRE(ball->Predict(TickSpan{ticks}) == ball->Predict(static_cast<int>(ticks * 10)));
      REQUIRE(ball->Predict(TickSpan{ticks}) == ball->Predict(static_cast<int>(ticks * 10 + 9)));
      REQUIRE(image.GetBallPrediction(TickSpan{ticks}, SimulationAccess::NowOf(simulation), *ball) ==
              image.GetBallPrediction(static_cast<int>(ticks * 10), SimulationAccess::NowOf(simulation), *ball));
    }
    const auto rng = SimulationAccess::RngOf(simulation).engine();
    simulation.AdvanceTime(TickSpan{17});
    REQUIRE(image.GetAge(SimulationAccess::NowOf(simulation)) == TickSpan{17});
    REQUIRE(SimulationAccess::RngOf(simulation).engine() == rng);
    REQUIRE(ball->Predict(-1) == ball->Predict(TickSpan{}));
    REQUIRE(ball->Predict(std::numeric_limits<int>::min()) == ball->Predict(TickSpan{}));
    for (int ms : {-1000, -171, -170, -169, -10, -1, 0, 1, 9, 10, 2999, 3000, 4000}) {
      const int old_index = std::clamp(ms + 170, 0, 2990) / 10;
      const auto expected = image.ballPredictions[old_index].EnforceMaximumDeviation(
          ball->Predict(ms), image.maxDistanceDeviation);
      REQUIRE(image.GetBallPrediction(ms, SimulationAccess::NowOf(simulation), *ball) == expected);
    }
    const auto last = ball_timing::kPredictionHorizon - TickSpan{1};
    const TickSpan huge{std::numeric_limits<std::uint64_t>::max()};
    REQUIRE(ball->Predict(huge) == ball->Predict(last));
    REQUIRE(image.GetBallPrediction(huge, SimulationAccess::NowOf(simulation), *ball) ==
            image.GetBallPrediction(last, SimulationAccess::NowOf(simulation), *ball));
    simulation.AdvanceTime(Seconds(5));
    REQUIRE(image.GetAge(SimulationAccess::NowOf(simulation)) == TickSpan{517});
    REQUIRE(image.GetBallPrediction(TickSpan{}, SimulationAccess::NowOf(simulation), *ball) == image.ballPredictions.back().EnforceMaximumDeviation(
        ball->Predict(TickSpan{}), image.maxDistanceDeviation));
  }
}
}  // namespace
