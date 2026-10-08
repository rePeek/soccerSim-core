#include <array>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "app/fixtures/default_teams.hpp"
#include "football/ball/ball.hpp"
#include "sim/player/player_ball_contact.hpp"
#include "sim/event/ball_touch_sink.hpp"
#include "sim/observation/mentalimage.hpp"
#include "sim/observation/mentalimage_sampling.hpp"
#include "sim/simulation.hpp"
#include "sim/testing/simulation_access.hpp"
using football::sim::testing::SimulationAccess;

namespace {
using namespace football::sim;
using blunted::Vector3;

static_assert(std::is_constructible_v<Ball, const football::model::Pitch&>);

template<class T>
concept HasPublicTickEntry = requires(T& value) { value.Step(PlayerControlSet{}); } ||
                            requires(T& value) { value.Process(); };

struct ContactFixture {
  Simulation simulation;
  std::array<Team*, 2> teams;
  Player* home;
  Player* away;
  // Borrows the owner's write-only publication port for direct contact calls.
  BallTouchSink* touch_sink = nullptr;

  explicit ContactFixture(bool reverse) {
    MatchOptions options;
    options.reverse_team_processing = reverse;
    simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
        football::app::fixtures::MakeDefaultAwayTeam(),
        football::model::MakeLegacyPitch(), options);
    touch_sink = &SimulationAccess::EventsOf(simulation);
    teams = {SimulationAccess::TeamOf(simulation, 0), SimulationAccess::TeamOf(simulation, 1)};
    // Native kickoff placement initializes the Movement action publication.
    for (int i = 0; i < 40; ++i) simulation.Step({});
    home = teams[0]->GetAllPlayers()[1];
    away = teams[1]->GetAllPlayers()[1];
    for (auto* team : teams) {
      for (auto* player : team->GetAllPlayers()) {
        player->ResetPosition(Vector3(20, 20, 0), Vector3(0));
      }
    }
    home->ResetPosition(Vector3(0), Vector3(1, 0, 0));
    away->ResetPosition(Vector3(0), Vector3(1, 0, 0));
    simulation.AdvanceTime(Seconds(1));
    auto& touch_sink = SimulationAccess::EventsOf(simulation);
    touch_sink.OnBallTouched({SimulationAccess::NowOf(simulation), teams[1]->GetAllPlayers()[2], teams[1],
        e_TouchType_Intentional_Kicked});
    SimulationAccess::BallOf(simulation)->SetPosition(Vector3(0.05f, 0, 1.0f), SimulationAccess::BallEnvironmentOf(simulation));
    SimulationAccess::BallOf(simulation)->SetMomentum(Vector3(-8, 0, 0), SimulationAccess::BallEnvironmentOf(simulation));
  }

  BallPlayerContactInputs Inputs(Tick last_collision = {}) {
    return {teams, SimulationAccess::FirstTeamOf(simulation), SimulationAccess::LastTouchTeamIDOf(simulation), {},
            SimulationAccess::NowOf(simulation), last_collision, &*touch_sink, SimulationAccess::TouchesOf(simulation)};
  }
};

TEST_CASE("Ball physics runs without a Match and takes netting facts per call",
          "[sim][ball][contact]") {
  Ball ball(football::model::MakeLegacyPitch());
  const football::ball::BallEnvironment outside_goal{};
  ball.SetPosition(Vector3(0, 0, 0), outside_goal);
  ball.Touch(Vector3(8, 1, 3), outside_goal);
  REQUIRE(ball.Predict(0).coords[2] == 0.11f);
  REQUIRE(ball.GetMovement() == Vector3(8, 1, 3));
  const auto next = ball.Predict(TickSpan{1});
  ball.Process(outside_goal);
  // Preserve the legacy cache publication: Process moves the physical buffer;
  // the following calculation publishes that buffer as prediction sample zero.
  ball.CalculatePrediction(outside_goal);
  REQUIRE(ball.Predict(0) == next);

  ball.SetPosition(Vector3(58.5f, 0, 1.0f), outside_goal);
  ball.Touch(Vector3(3, 0, 0), outside_goal);
  const auto outside = ball.CalculatePrediction(outside_goal);
  const auto inside = ball.CalculatePrediction(football::ball::BallEnvironment{true});
  REQUIRE(inside.momentum != outside.momentum);
  // No latched rule fact in Ball: subsequent calls use only their argument.
  REQUIRE(ball.CalculatePrediction(outside_goal).momentum == outside.momentum);
}

TEST_CASE("body contacts preserve the cooldown boundary and ordered touch feedback",
          "[sim][ball][contact]") {
  for (bool reverse : {false, true}) {
    ContactFixture fixture(reverse);
    std::array<Player*, 2> players{fixture.home, fixture.away};
    REQUIRE(fixture.home->GetSimulationActionState().type == e_FunctionType_Movement);
    REQUIRE(fixture.away->GetSimulationActionState().type == e_FunctionType_Movement);
    REQUIRE_FALSE(fixture.home->HasUniquePossession());
    REQUIRE_FALSE(fixture.away->HasUniquePossession());
    const auto rng = SimulationAccess::RngOf(fixture.simulation).engine();
    const auto velocity = SimulationAccess::BallOf(fixture.simulation)->GetMovement();
    std::vector<Vector3> predictions;
    SimulationAccess::BallOf(fixture.simulation)->GetPredictionArray(predictions);
    const auto last_collision = SimulationAccess::NowOf(fixture.simulation) - TickSpan{15};
    const auto blocked = ResolveBallPlayerContacts(*SimulationAccess::BallOf(fixture.simulation), players,
                                                   fixture.Inputs(last_collision));
    REQUIRE_FALSE(blocked.impulse);
    REQUIRE(fixture.home->GetLastTouchType() != e_TouchType_Accidental);

    fixture.simulation.AdvanceTime(TickSpan{1});
    const auto result = ResolveBallPlayerContacts(*SimulationAccess::BallOf(fixture.simulation), players,
                                                  fixture.Inputs(last_collision));
    REQUIRE(result.impulse);
    REQUIRE(result.rotation_bias > 0.0f);
    REQUIRE(result.impulse->GetLength() <= velocity.GetLength());
    REQUIRE(fixture.home->GetLastTouchTick() == SimulationAccess::NowOf(fixture.simulation));
    // Away becomes eligible only after Home publishes its accidental touch.
    REQUIRE(fixture.away->GetLastTouchTick() == SimulationAccess::NowOf(fixture.simulation));
    REQUIRE(fixture.home->GetLastTouchType() == e_TouchType_Accidental);
    REQUIRE(fixture.away->GetLastTouchType() == e_TouchType_Accidental);
    REQUIRE(SimulationAccess::LastTouchTeamIDOf(fixture.simulation) == 1);
    REQUIRE(SimulationAccess::RngOf(fixture.simulation).engine() == rng);
    REQUIRE(SimulationAccess::BallOf(fixture.simulation)->GetMovement() == velocity);
    std::vector<Vector3> after;
    SimulationAccess::BallOf(fixture.simulation)->GetPredictionArray(after);
    REQUIRE(after == predictions); // Runtime, not the resolver, applies the impulse.
  }
}

TEST_CASE("controlled body contacts request a player action without a random bounce",
          "[sim][ball][contact]") {
  ContactFixture fixture(false);
  fixture.teams[0]->SetDesignatedTeamPossessionPlayer(fixture.home);
  // The opponent has a fresh team touch, while the global latest-team record
  // refers to Home's stale touch. Preserve this legacy controlled-contact gate.
  auto& touches = SimulationAccess::TouchesOf(fixture.simulation);
  touches.last_team = 0;
  touches.last_team_by_type[e_TouchType_Intentional_Kicked] = 0;
  fixture.home->ResetControlledBallCollisionTrigger();
  const auto rng = SimulationAccess::RngOf(fixture.simulation).engine();
  std::array<Player*, 1> players{fixture.home};
  const auto result = ResolveBallPlayerContacts(*SimulationAccess::BallOf(fixture.simulation), players, fixture.Inputs());
  REQUIRE_FALSE(result.impulse);
  REQUIRE(fixture.home->IsControlledBallCollisionTriggered());
  REQUIRE(fixture.home->GetLastTouchType() != e_TouchType_Accidental);
  REQUIRE(SimulationAccess::RngOf(fixture.simulation).engine() == rng);
}

TEST_CASE("contact history sampling and prediction accept explicit time and Ball",
          "[sim][ball][contact][observation]") {
  using observation::MentalImageSampleIndex;
  REQUIRE(MentalImageSampleIndex(3, TickSpan{4}) == 0);
  REQUIRE(MentalImageSampleIndex(3, TickSpan{5}) == 1);
  REQUIRE(MentalImageSampleIndex(3, TickSpan{15}) == 2);
  REQUIRE(MentalImageSampleIndex(3, std::chrono::milliseconds{-1}) == 0);
  REQUIRE(MentalImageSampleIndex(3, std::chrono::milliseconds{49}) == 0);
  REQUIRE(MentalImageSampleIndex(3, std::chrono::milliseconds{50}) == 1);
  REQUIRE(MentalImageSampleIndex(3, std::chrono::milliseconds::max()) == 2);
  REQUIRE_THROWS_AS(MentalImageSampleIndex(0, TickSpan{}), std::logic_error);

  Ball ball(football::model::MakeLegacyPitch());
  ball.ResetSituation(Vector3(0));
  MentalImage image; // No Match attached.
  image.captured_tick = Tick{5};
  image.maxDistanceDeviation = 10000.0f;
  image.ballPredictions.resize(ball_timing::kPredictionHorizon.value);
  for (std::size_t i = 0; i < image.ballPredictions.size(); ++i) {
    image.ballPredictions[i] = Vector3(static_cast<float>(i), 0, 0.11f);
  }
  REQUIRE(image.GetBallPrediction(Seconds(1), Tick{17}, ball) == image.ballPredictions[112]);
  REQUIRE(image.GetBallPrediction(TickSpan{std::numeric_limits<std::uint64_t>::max()},
                                 Tick{17}, ball) == image.ballPredictions.back());
}

}  // namespace
