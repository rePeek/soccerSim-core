#include <limits>

#include <catch2/catch_test_macros.hpp>

#include "app/fixtures/default_teams.hpp"
#include "sim/ai_support/mentalimage.hpp"
#include "sim/match.hpp"
#include "sim/simulation.hpp"

namespace {
using namespace football::sim;

TEST_CASE("Ball samples and mental-image ages use ticks without changing quantization",
          "[sim][tick][prediction]") {
  for (bool reverse : {false, true}) {
    MatchOptions options;
    options.reverse_team_processing = reverse;
    Simulation simulation;
    simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
                    football::app::fixtures::MakeDefaultAwayTeam(),
                    football::model::MakeLegacyPitch(), options);
    Match* match = simulation.match();
    Ball* ball = match->GetBall();
    ball->SetPosition(blunted::Vector3(0, 0, 10));
    ball->SetMomentum(blunted::Vector3(8, 1, 3));
    MentalImage image(match);
    REQUIRE(image.captured_tick == match->GetTimelineTick());
    REQUIRE(image.GetAge() == TickSpan{});
    REQUIRE(image.ballPredictions.size() == ball_timing::kPredictionHorizon.value);
    for (std::uint64_t ticks = 0; ticks < 400; ++ticks) {
      REQUIRE(ball->Predict(TickSpan{ticks}) == ball->Predict(static_cast<int>(ticks * 10)));
      REQUIRE(ball->Predict(TickSpan{ticks}) == ball->Predict(static_cast<int>(ticks * 10 + 9)));
      REQUIRE(image.GetBallPrediction(TickSpan{ticks}) == image.GetBallPrediction(static_cast<int>(ticks * 10)));
    }
    const auto rng = match->rng().engine();
    match->AdvanceTime(TickSpan{17});
    REQUIRE(image.GetAge() == TickSpan{17});
    REQUIRE(match->rng().engine() == rng);
    REQUIRE(ball->Predict(-1) == ball->Predict(TickSpan{}));
    REQUIRE(ball->Predict(std::numeric_limits<int>::min()) == ball->Predict(TickSpan{}));
    for (int ms : {-1000, -171, -170, -169, -10, -1, 0, 1, 9, 10, 2999, 3000, 4000}) {
      const int old_index = std::clamp(ms + 170, 0, 2990) / 10;
      const auto expected = image.ballPredictions[old_index].EnforceMaximumDeviation(
          ball->Predict(ms), image.maxDistanceDeviation);
      REQUIRE(image.GetBallPrediction(ms) == expected);
    }
    const auto last = ball_timing::kPredictionHorizon - TickSpan{1};
    const TickSpan huge{std::numeric_limits<std::uint64_t>::max()};
    REQUIRE(ball->Predict(huge) == ball->Predict(last));
    REQUIRE(image.GetBallPrediction(huge) == image.GetBallPrediction(last));
    match->AdvanceTime(Seconds(5));
    REQUIRE(image.GetAge() == TickSpan{517});
    REQUIRE(image.GetBallPrediction(TickSpan{}) == image.ballPredictions.back().EnforceMaximumDeviation(
        ball->Predict(TickSpan{}), image.maxDistanceDeviation));
  }
}
}  // namespace
