#include <array>
#include <stdexcept>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "app/fixtures/default_teams.hpp"
#include "default_ai_fixture.hpp"
#include "sim/ball/ball.hpp"
#include "sim/observation/mentalimage_sampling.hpp"
#include "sim/simulation.hpp"

namespace {
using namespace football::sim;
using blunted::Vector3;

TEST_CASE("history sampling and newest-ball refresh use explicit borrowed images",
          "[sim][history]") {
  std::array<MentalImage, 3> images;
  for (std::size_t i = 0; i < images.size(); ++i) {
    images[i].captured_tick = Tick{30 - i * 10};
    images[i].ballPredictions = {Vector3(float(i), 2, 3)};
  }
  REQUIRE(observation::SampleMentalImage(images, TickSpan{5}) == &images[1]);
  REQUIRE(observation::SampleMentalImage(images, std::chrono::milliseconds{150}) == &images[2]);
  const auto older = images[1].ballPredictions;
  const auto oldest = images[2].ballPredictions;
  Ball ball(football::model::MakeLegacyPitch());
  ball.ResetSituation(Vector3(4, 5, 0));
  std::vector<Vector3> predictions;
  ball.GetPredictionArray(predictions);
  observation::RefreshLatestMentalImageBallPredictions(images, ball);
  REQUIRE(images[0].ballPredictions == predictions);
  REQUIRE(images[0].captured_tick == Tick{30});
  REQUIRE(images[1].ballPredictions == older);
  REQUIRE(images[2].ballPredictions == oldest);
  observation::RefreshLatestMentalImageBallPredictions({}, ball);
  REQUIRE_THROWS_AS(observation::SampleMentalImage({}, TickSpan{}), std::logic_error);
}

void Init(Simulation& simulation, bool reverse) {
  MatchOptions options;
  options.reverse_team_processing = reverse;
  simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
      football::app::fixtures::MakeDefaultAwayTeam(),
      football::model::MakeLegacyPitch(), options);
  football::test::TakeKickOff(simulation);
}

TEST_CASE("Simulation owns the sole history across touch mirror reset and Stop Init",
          "[sim][history]") {
  for (bool reverse : {false, true}) {
    Simulation simulation, other;
    REQUIRE_THROWS_AS(simulation.GetMentalImage(TickSpan{}), std::logic_error);
    Init(simulation, reverse);
    Init(other, reverse);
    Match& match = *simulation.match();
    const auto other_capture = other.GetMentalImage(TickSpan{})->captured_tick;
    const auto other_predictions = other.GetMentalImage(TickSpan{})->ballPredictions;
    for (TickSpan age : {TickSpan{}, TickSpan{10}, TickSpan{20}}) {
      REQUIRE(simulation.GetMentalImage(age) == match.GetMentalImage(age));
    }
    const auto oldest_predictions = simulation.GetMentalImage(TickSpan{20})->ballPredictions;
    const auto latest_capture = simulation.GetMentalImage(TickSpan{})->captured_tick;
    match.TouchBall(Vector3(7, 2, 1));
    std::vector<Vector3> ball_predictions;
    match.GetBall()->GetPredictionArray(ball_predictions);
    REQUIRE(simulation.GetMentalImage(TickSpan{})->ballPredictions == ball_predictions);
    REQUIRE(simulation.GetMentalImage(TickSpan{})->captured_tick == latest_capture);
    REQUIRE(simulation.GetMentalImage(TickSpan{20})->ballPredictions == oldest_predictions);

    const auto oldest = *simulation.GetMentalImage(TickSpan{20});
    const auto rng = match.rng().engine();
    match.Mirror(true, false, true);
    auto* mirrored = simulation.GetMentalImage(TickSpan{20});
    REQUIRE(mirrored->ballPredictions_mirrored != oldest.ballPredictions_mirrored);
    for (std::size_t i = 0; i < oldest.ballPredictions.size(); ++i) {
      auto expected = oldest.ballPredictions[i]; expected.Mirror();
      REQUIRE(mirrored->ballPredictions[i] == expected);
    }
    for (std::size_t i = 0; i < oldest.players.size(); ++i) {
      auto expected = oldest.players[i];
      if (expected.player->GetTeamID() == 0) expected.Mirror();
      REQUIRE(mirrored->players[i].position == expected.position);
      REQUIRE(mirrored->players[i].movement == expected.movement);
    }
    match.Mirror(true, false, true);
    REQUIRE(simulation.GetMentalImage(TickSpan{20})->ballPredictions == oldest.ballPredictions);
    REQUIRE(match.rng().engine() == rng);

    // Permanent end changes must transform the owned history too, not just actors.
    if (match.GetTimelineTick().value % observation::kMentalImageCadence.value == 0)
      match.AdvanceTime(TickSpan{1});
    match.RequestChangeOfEnds();
    simulation.Step({});
    REQUIRE(simulation.GetMentalImage(TickSpan{20})->captured_tick == oldest.captured_tick);
    auto flipped = oldest.ballPredictions;
    for (auto& position : flipped) position.Mirror();
    REQUIRE(simulation.GetMentalImage(TickSpan{20})->ballPredictions == flipped);

    match.ResetSituation(Vector3(0));
    REQUIRE_THROWS_AS(simulation.GetMentalImage(TickSpan{}), std::logic_error);
    REQUIRE_THROWS_AS(match.GetMentalImage(TickSpan{}), std::logic_error);
    simulation.Step({});
    REQUIRE(simulation.GetMentalImage(TickSpan{20}) == simulation.GetMentalImage(TickSpan{}));
    REQUIRE(other.GetMentalImage(TickSpan{})->captured_tick == other_capture);
    REQUIRE(other.GetMentalImage(TickSpan{})->ballPredictions == other_predictions);
    simulation.Stop();
    REQUIRE_THROWS_AS(simulation.GetMentalImage(TickSpan{}), std::logic_error);
    Init(simulation, reverse);
    REQUIRE(simulation.GetMentalImage(TickSpan{}) == simulation.match()->GetMentalImage(TickSpan{}));
    REQUIRE(other.GetMentalImage(TickSpan{})->ballPredictions == other_predictions);
  }
}

}  // namespace
