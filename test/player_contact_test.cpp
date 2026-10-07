#include <array>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "app/fixtures/default_teams.hpp"
#include "sim/ball/ball.hpp"
#include "sim/match/match.hpp"
#include "sim/player/player_contact.hpp"
#include "sim/simulation.hpp"

namespace {
using namespace football::sim;
using blunted::Vector3;

struct PlayerContactFixture {
  Simulation simulation;
  Ball ball{football::model::MakeLegacyPitch()};
  std::array<Player*, 3> players;

  explicit PlayerContactFixture(bool reverse) {
    MatchOptions options;
    options.reverse_team_processing = reverse;
    simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
        football::app::fixtures::MakeDefaultAwayTeam(),
        football::model::MakeLegacyPitch(), options);
    Match& match = *simulation.match();
    players = {match.GetTeam(match.FirstTeam())->GetAllPlayers()[1],
               match.GetTeam(match.SecondTeam())->GetAllPlayers()[1],
               match.GetTeam(match.FirstTeam())->GetAllPlayers()[2]};
    ball.ResetSituation(Vector3(20, 20, 0));
    Reset();
  }

  void Reset() {
    for (std::size_t i = 0; i < players.size(); ++i) {
      const Vector3 position(static_cast<float>(i) * 0.5f, 0, 0);
      players[i]->ResetPosition(position, position + Vector3(1, 0, 0));
      REQUIRE(players[i]->GetMovement() == Vector3(0));
    }
  }

  Referee& referee() { return *simulation.match()->GetReferee(); }

  std::array<Vector3, 3> Positions() const {
    return {players[0]->GetPosition(), players[1]->GetPosition(), players[2]->GetPosition()};
  }
};

TEST_CASE("player contacts accept empty and single-player spans without side effects",
          "[sim][player][contact]") {
  PlayerContactFixture fixture(false);
  const auto before = fixture.Positions();
  const auto rng = fixture.simulation.match()->rng().engine();
  ResolvePlayerContacts({Tick{19}, {}, fixture.ball, nullptr}, fixture.referee());
  ResolvePlayerContacts({Tick{19}, std::span<Player* const>(fixture.players.data(), 1),
                         fixture.ball, fixture.players[0]}, fixture.referee());
  REQUIRE(fixture.Positions() == before);
  REQUIRE(fixture.simulation.match()->rng().engine() == rng);
}

TEST_CASE("player contacts mutate pairs in caller order before the next pair",
          "[sim][player][contact]") {
  for (bool reverse : {false, true}) {
    PlayerContactFixture fixture(reverse);
    const auto before = fixture.Positions();
    const auto rng = fixture.simulation.match()->rng().engine();
    std::vector<Vector3> predictions;
    fixture.ball.GetPredictionArray(predictions);
    ResolvePlayerContacts({Tick{19}, fixture.players, fixture.ball, nullptr}, fixture.referee());
    const auto sweep = fixture.Positions();
    REQUIRE(sweep != before);
    REQUIRE(fixture.simulation.match()->rng().engine() == rng);
    std::vector<Vector3> after;
    fixture.ball.GetPredictionArray(after);
    REQUIRE(after == predictions);

    fixture.Reset();
    // These stationary actors have no movement-sharing offset. Each two-player
    // invocation is therefore an independent oracle for the in-place pair sweep.
    for (std::size_t i = 0; i < 2; ++i) {
      for (std::size_t j = i + 1; j < 3; ++j) {
        const std::array<Player*, 2> pair{fixture.players[i], fixture.players[j]};
        ResolvePlayerContacts({Tick{19}, pair, fixture.ball, nullptr}, fixture.referee());
      }
    }
    REQUIRE(fixture.Positions() == sweep);

    fixture.Reset();
    const std::array<Player*, 3> reversed{
        fixture.players[2], fixture.players[1], fixture.players[0]};
    ResolvePlayerContacts({Tick{19}, reversed, fixture.ball, nullptr}, fixture.referee());
    REQUIRE(fixture.Positions() != sweep); // No sorting or frozen-position batch.
  }
}

TEST_CASE("player contacts use the explicit possession designation and Ball",
          "[sim][player][contact]") {
  PlayerContactFixture fixture(false);
  const std::array<Player*, 2> pair{fixture.players[0], fixture.players[1]};
  ResolvePlayerContacts({Tick{19}, pair, fixture.ball, pair[0]}, fixture.referee());
  const auto first_designated = fixture.Positions();
  fixture.Reset();
  ResolvePlayerContacts({Tick{19}, pair, fixture.ball, pair[1]}, fixture.referee());
  REQUIRE(fixture.Positions() != first_designated);
  REQUIRE(fixture.simulation.match()->GetDesignatedPossessionPlayer() != pair[0]);
  REQUIRE(fixture.simulation.match()->GetDesignatedPossessionPlayer() != pair[1]);
}

}  // namespace
