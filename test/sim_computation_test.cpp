#include <array>
#include <cmath>

#include <catch2/catch_test_macros.hpp>

#include "app/fixtures/default_teams.hpp"
#include "sim/testing/simulation_access.hpp"
#include "sim/observation/mentalimage.hpp"
#include "football/ball/ball.hpp"
#include "sim/player/kick_targeting.hpp"
#include "sim/player/player.hpp"
#include "sim/query/player_query.hpp"
#include "sim/query/reachability.hpp"
#include "sim/rules/offside.hpp"
#include "sim/player/player_motion_constants.hpp"
#include "sim/simulation.hpp"
#include "sim/team/team.hpp"

namespace {

using football::sim::testing::SimulationAccess;

// Computations require no decision implementation or factory.
struct Runtime {
  Simulation simulation;

  Runtime() {
    simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
                    football::app::fixtures::MakeDefaultAwayTeam(),
                    football::model::MakeLegacyPitch(), MatchOptions{});
  }

};

}  // namespace

TEST_CASE("reachability retains legacy near and far estimates", "[sim][query]") {
  // Captured by compiling the pre-extraction function from d1418b7. These are
  // integer results, not toleranced floats; include optimistic/zero-time paths.
  struct Sample {
    float distance, speed;
    unsigned int usual, optimistic;
  };
  constexpr std::array<Sample, 18> samples{{
      {0.f, 0.f, 0, 0}, {0.f, 2.f, 0, 0}, {0.f, -2.f, 0, 0},
      {0.05f, 0.f, 2, 2}, {0.05f, 2.f, 2, 2}, {0.05f, -2.f, 2, 2},
      {0.5f, 0.f, 150, 0}, {0.5f, 2.f, 40, 0}, {0.5f, -2.f, 420, 0},
      {3.f, 0.f, 703, 610}, {3.f, 2.f, 600, 500}, {3.f, -2.f, 827, 744},
      {10.f, 0.f, 1634, 1551}, {10.f, 2.f, 1537, 1454},
      {10.f, -2.f, 1757, 1675}, {60.f, 0.f, 10000, 9800},
      {60.f, 2.f, 9933, 9733}, {60.f, -2.f, 10067, 9867},
  }};
  for (const auto &sample : samples) {
    INFO(sample.distance << " " << sample.speed);
    const auto result = football::sim::query::GetTimeNeededForDistance_ms(
        Vector3(0), Vector3(sample.speed, 0, 0), Vector3(sample.distance, 0, 0),
        8.f, true);
    CHECK(result.usual_ms == sample.usual);
    CHECK(result.optimistic_ms == sample.optimistic);
  }
  const auto bounded = football::sim::query::GetTimeNeededForDistance_ms(
      Vector3(0), Vector3(0), Vector3(10, 0, 0), 8.f, true, football::sim::TickSpan{10});
  CHECK(bounded.usual_ms == 1667);
  CHECK(bounded.optimistic_ms == 110);
}

TEST_CASE("player queries retain roster ties append and eligibility", "[sim][query]") {
  Runtime runtime;
  Team *team = SimulationAccess::TeamOf(runtime.simulation, 0);
  const auto &players = team->GetAllPlayers();
  REQUIRE(players.size() >= 3);
  for (Player *player : players) player->ResetPosition(Vector3(20, 0, 0), Vector3(0));
  players[0]->ResetPosition(Vector3(1, 0, 0), Vector3(0));
  players[1]->ResetPosition(Vector3(-1, 0, 0), Vector3(0));

  CHECK(football::sim::query::GetClosestPlayer(team, Vector3(0)) == players[0]);
  CHECK(football::sim::query::GetClosestPlayer(team, Vector3(0), players[0]) == players[1]);
  std::vector<Player*> result{players[2]};
  football::sim::query::GetClosestPlayers(team, Vector3(0), result, 2);
  REQUIRE(result.size() == 3);
  CHECK(result[0] == players[2]);
  CHECK(result[1] == players[0]);
  CHECK(result[2] == players[1]);

  players[0]->Deactivate(*SimulationAccess::BallOf(runtime.simulation), SimulationAccess::NowOf(runtime.simulation));
  CHECK(football::sim::query::GetClosestPlayer(team, Vector3(0)) == players[1]);
  result.clear();
  football::sim::query::GetClosestPlayers(team, Vector3(0), result, 1);
  REQUIRE(result.size() == 1);
  CHECK(result[0] == players[1]);
}

TEST_CASE("offside geometry follows second defender ball and halfway line", "[sim][rules]") {
  Runtime runtime;
  for (int teamID : {0, 1}) {
    Team *team = SimulationAccess::TeamOf(runtime.simulation, teamID);
    const int side = team->GetDynamicSide();
    const auto &players = team->GetAllPlayers();
    for (Player *player : players) player->ResetPosition(Vector3(0), Vector3(0));
    players[0]->ResetPosition(Vector3(40 * side, 0, 0), Vector3(0));
    players[1]->ResetPosition(Vector3(30 * side, 0, 0), Vector3(0));
    SimulationAccess::BallOf(runtime.simulation)->SetPosition(Vector3(0, 0, 0.11f), SimulationAccess::BallEnvironmentOf(runtime.simulation));
    SimulationAccess::BallOf(runtime.simulation)->CalculatePrediction(SimulationAccess::BallEnvironmentOf(runtime.simulation));
    std::vector<Player*> snapshot_players;
    SimulationAccess::TeamOf(runtime.simulation, SimulationAccess::FirstTeamOf(runtime.simulation))->GetActivePlayers(snapshot_players);
    SimulationAccess::TeamOf(runtime.simulation, SimulationAccess::SecondTeamOf(runtime.simulation))->GetActivePlayers(snapshot_players);
    MentalImage image(SimulationAccess::NowOf(runtime.simulation), snapshot_players, *SimulationAccess::BallOf(runtime.simulation));
    CHECK(football::sim::rules::GetOffsideLine(image, SimulationAccess::NowOf(runtime.simulation),
        SimulationAccess::PitchOf(runtime.simulation),
        *SimulationAccess::BallOf(runtime.simulation), teamID, side) == 30 * side);

    SimulationAccess::BallOf(runtime.simulation)->SetPosition(Vector3(35 * side, 0, 0.11f), SimulationAccess::BallEnvironmentOf(runtime.simulation));
    SimulationAccess::BallOf(runtime.simulation)->CalculatePrediction(SimulationAccess::BallEnvironmentOf(runtime.simulation));
    MentalImage ballAhead(SimulationAccess::NowOf(runtime.simulation), snapshot_players, *SimulationAccess::BallOf(runtime.simulation));
    CHECK(football::sim::rules::GetOffsideLine(ballAhead, SimulationAccess::NowOf(runtime.simulation),
        SimulationAccess::PitchOf(runtime.simulation),
        *SimulationAccess::BallOf(runtime.simulation), teamID, side) == 35 * side);

    for (Player *player : players) player->ResetPosition(Vector3(-10 * side, 0, 0), Vector3(0));
    SimulationAccess::BallOf(runtime.simulation)->SetPosition(Vector3(-5 * side, 0, 0.11f), SimulationAccess::BallEnvironmentOf(runtime.simulation));
    SimulationAccess::BallOf(runtime.simulation)->CalculatePrediction(SimulationAccess::BallEnvironmentOf(runtime.simulation));
    MentalImage otherHalf(SimulationAccess::NowOf(runtime.simulation), snapshot_players, *SimulationAccess::BallOf(runtime.simulation));
    CHECK(football::sim::rules::GetOffsideLine(otherHalf, SimulationAccess::NowOf(runtime.simulation),
        SimulationAccess::PitchOf(runtime.simulation),
        *SimulationAccess::BallOf(runtime.simulation), teamID, side) == 0.f);

    // The supplied movement is clamped to the live movement deviation, then the
    // caller's horizon extrapolates it; the predicate reads only explicit facts.
    players[0]->ResetPosition(Vector3(40 * side, 0, 0), Vector3(0));
    players[1]->ResetPosition(Vector3(30 * side, 0, 0), Vector3(0));
    MentalImage static_image(SimulationAccess::NowOf(runtime.simulation), snapshot_players, *SimulationAccess::BallOf(runtime.simulation));
    for (auto& entry : static_image.players) {
      if (entry.player->GetTeamID() == teamID) entry.movement = Vector3(10 * side, 0, 0);
    }
    CHECK(football::sim::rules::GetOffsideLine(static_image, SimulationAccess::NowOf(runtime.simulation),
        SimulationAccess::PitchOf(runtime.simulation),
        *SimulationAccess::BallOf(runtime.simulation), teamID, side, 1000) == (30 + walkVelocity) * side);
  }
}

TEST_CASE("kick targeting preserves forced recipients and manual shot direction", "[sim][mechanics]") {
  Runtime runtime;
  const auto &players = SimulationAccess::TeamOf(runtime.simulation, 0)->GetAllPlayers();
  Player *kicker = players[1];
  Player *recipient = players[2];
  kicker->ResetPosition(Vector3(0), Vector3(10, 0, 0));
  recipient->ResetPosition(Vector3(15, 0, 0), Vector3(0));
  for (e_FunctionType type : {e_FunctionType_ShortPass, e_FunctionType_LongPass,
                              e_FunctionType_HighPass}) {
    Vector3 direction;
    float power = 0;
    Player *target = nullptr;
    football::sim::mechanics::GetPass(kicker, type, Vector3(1, 0, 0), 0.25f,
                                     0.5f, 0.5f, direction, power, target, recipient);
    CHECK(target == recipient);
    CHECK(std::isfinite(power));
    CHECK(power > 0.f);
    CHECK(direction.coords[2] > 0.f);
  }
  const auto shot = football::sim::mechanics::GetShotDirection(kicker, SimulationAccess::PitchOf(runtime.simulation), Vector3(0, 1, 0), 0.f);
  CHECK(shot.coords[0] == 0.f);
  CHECK(shot.coords[1] == 1.f);
  CHECK(shot.coords[2] == 0.f);
}
