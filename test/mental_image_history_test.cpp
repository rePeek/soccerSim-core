#include <array>
#include <stdexcept>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "app/fixtures/default_teams.hpp"
#include "default_ai_fixture.hpp"
#include "sim/ball/ball.hpp"
#include "sim/observation/mentalimage_sampling.hpp"
#include "sim/simulation.hpp"

#include <type_traits>

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
    simulation.Mirror(true, false, true);
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
    simulation.Mirror(true, false, true);
    REQUIRE(simulation.GetMentalImage(TickSpan{20})->ballPredictions == oldest.ballPredictions);
    REQUIRE(match.rng().engine() == rng);

    // Permanent end changes must transform the owned history too, not just actors.
    if (match.GetTimelineTick().value % observation::kMentalImageCadence.value == 0)
      simulation.AdvanceTime(TickSpan{1});
    match.RequestChangeOfEnds();
    simulation.Step({});
    REQUIRE(simulation.GetMentalImage(TickSpan{20})->captured_tick == oldest.captured_tick);
    auto flipped = oldest.ballPredictions;
    for (auto& position : flipped) position.Mirror();
    REQUIRE(simulation.GetMentalImage(TickSpan{20})->ballPredictions == flipped);

    simulation.ResetSituation(Vector3(0));
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

static_assert(!std::is_constructible_v<MentalImage, Match*>);
template<class T> concept HasHistoryMirror = requires(T& owner) { owner.Mirror(true, true, true); };
template<class T> concept HasHistoryReset = requires(T& owner) { owner.ResetSituation(Vector3(0)); };
static_assert(!HasHistoryMirror<Match>);
static_assert(!HasHistoryReset<Match>);

TEST_CASE("mental images capture explicit ordered inputs and sample with explicit time and Ball",
          "[sim][history][snapshot]") {
  Simulation simulation;
  Init(simulation, true);
  auto& match = *simulation.match();
  const auto rng = match.rng().engine();
  auto* first = match.GetTeam(match.FirstTeam())->GetAllPlayers()[1];
  auto* second = match.GetTeam(match.SecondTeam())->GetAllPlayers()[2];
  std::array<Player*, 2> inputs{second, first};
  Ball ball(football::model::MakeLegacyPitch());
  ball.ResetSituation(Vector3(8, 3, 0));
  MentalImage image(Tick{100}, inputs, ball);
  REQUIRE(image.captured_tick == Tick{100});
  REQUIRE(image.GetAge(Tick{117}) == TickSpan{17});
  REQUIRE(image.players.size() == 2);
  REQUIRE(image.players[0].player == second);
  REQUIRE(image.players[1].player == first);
  const auto captured_players = image.players;
  const auto captured_predictions = image.ballPredictions;
  second->OffsetPosition(Vector3(1, 2, 0));
  const auto sampled = image.GetPlayerImage(second, Tick{117});
  const auto& captured = captured_players[0];
  auto expected = captured.position + captured.movement * ToMilliseconds(TickSpan{17}) * 0.001f;
  expected = expected.EnforceMaximumDeviation(second->GetPosition(), image.maxDistanceDeviation);
  REQUIRE(sampled.position == expected);
  REQUIRE(image.players[0].position == captured.position);
  REQUIRE(image.ballPredictions == captured_predictions);
  const auto team_images = image.GetTeamPlayerImages(second->GetTeamID(), Tick{117});
  REQUIRE(team_images.size() == 1);
  REQUIRE(team_images[0].position == sampled.position);
  Ball other_ball(football::model::MakeLegacyPitch());
  other_ball.ResetSituation(Vector3(-25, -4, 0));
  REQUIRE(image.GetBallPrediction(TickSpan{}, Tick{117}, other_ball) ==
          captured_predictions[17].EnforceMaximumDeviation(other_ball.Predict(TickSpan{}),
                                                           image.maxDistanceDeviation));
  image.UpdateBallPredictions(other_ball);
  std::vector<Vector3> expected_predictions;
  other_ball.GetPredictionArray(expected_predictions);
  REQUIRE(image.ballPredictions == expected_predictions);
  REQUIRE(image.captured_tick == Tick{100});
  REQUIRE(image.players[0].position == captured.position);
  REQUIRE(match.rng().engine() == rng);

  // A Ball-only snapshot needs no live runtime owner, even after teardown.
  MentalImage detached(Tick{5}, {}, ball);
  simulation.Stop();
  REQUIRE(detached.GetAge(Tick{22}) == TickSpan{17});
  REQUIRE(detached.GetBallPrediction(0, Tick{22}, ball) ==
          detached.ballPredictions[17].EnforceMaximumDeviation(ball.Predict(0),
                                                              detached.maxDistanceDeviation));
}

TEST_CASE("Simulation mirror and reset reject stopped owners", "[sim][history]") {
  Simulation simulation;
  REQUIRE_THROWS_AS(simulation.Mirror(true, false, true), std::logic_error);
  REQUIRE_THROWS_AS(simulation.ResetSituation(Vector3(0)), std::logic_error);
  Init(simulation, false);
  simulation.Stop();
  REQUIRE_THROWS_AS(simulation.Mirror(true, false, true), std::logic_error);
  REQUIRE_THROWS_AS(simulation.ResetSituation(Vector3(0)), std::logic_error);
}

}  // namespace
