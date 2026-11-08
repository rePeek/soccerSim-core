#include <catch2/catch_test_macros.hpp>

#include <array>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include "football/animation/library.hpp"
#include "football/ball/ball.hpp"
#include "player/player.hpp"
#include "restart/restart_placement.hpp"

namespace {
const AnimationLibrary& Animations() {
  static const AnimationLibrary library = [] {
    AnimationLibrary value;
    if (!value.Load(GFOOTBALL_ANIMATION_CATALOG_PATH))
      throw std::runtime_error("cannot load kickoff test animations");
    return value;
  }();
  return library;
}

// Direct Player/TeamView composition: no Simulation, AI, referee or match tick.
struct KickoffTeam {
  std::vector<std::unique_ptr<Player>> owned;
  std::vector<Player*> players;
  std::vector<FormationEntry> formation;
  football::sim::TeamMatchState state;
  football::sim::TeamView view;

  KickoffTeam(int id, int defending, const football::pitch::Pitch& pitch) {
    const std::array<Vector3, 4> starts{
        Vector3(-.9f, 0, 0), Vector3(-.2f, .15f, 0),
        Vector3(.3f, -.2f, 0), Vector3(.8f, .8f, 0)};
    for (std::size_t i = 0; i < starts.size(); ++i) {
      football::player::PlayerDefinition definition;
      definition.id = static_cast<football::player::PlayerId>(id * 100 + i);
      auto actor = std::make_unique<Player>(id,
          static_cast<football::sim::TeamSide>(id), definition, Animations(), pitch);
      if (i < 3) actor->Activate(Vector3(5, 5, 0));
      players.push_back(actor.get());
      owned.push_back(std::move(actor));
      FormationEntry entry;
      entry.position = starts[i];
      entry.start_position = starts[i];
      entry.role = i == 0 ? e_PlayerRole_GK : e_PlayerRole_CF;
      formation.push_back(entry);
    }
    view.team_id = id;
    view.side = static_cast<football::sim::TeamSide>(id);
    view.defending_direction = defending;
    view.goalie = players[0];
    view.players = players;
    view.formation = formation;
    view.state = &state;
  }
};

void CheckFormation(const KickoffTeam& team, const KickoffTeam& source,
                    const football::pitch::Pitch& pitch) {
  for (std::size_t i = 0; i < 3; ++i) {
    const auto expected = source.formation[i].start_position *
        Vector3(-team.view.defending_direction * pitch.half_length(),
                -team.view.defending_direction * pitch.half_width(), 0);
    CHECK(team.players[i]->GetPosition() == expected);
    CHECK(team.players[i]->GetMovement() == Vector3(0));
    CHECK(team.players[i]->GetCurrentFrame() == 0);
  }
}
}  // namespace

TEST_CASE("Opening kickoff places each active roster from its own start formation",
          "[team][kickoff]") {
  const football::pitch::Pitch pitch;
  football::ball::Ball ball({}, pitch);
  for (int defending : {-1, 1}) {
    KickoffTeam home(0, defending, pitch);
    KickoffTeam away(1, -defending, pitch);
    const auto bench_position = home.players[3]->GetPosition();
    MatchOptions options;
    Player* taker = PositionKickOffPlayers(pitch, home.view, away.view,
                                         0, 0, ball, {}, options);
    CHECK(PositionKickOffPlayers(pitch, away.view, home.view,
                                 0, 0, ball, {}, options) == nullptr);
    CheckFormation(home, home, pitch);
    CheckFormation(away, away, pitch);
    // Player 1 is nearest the ball after final placement, not during the
    // intermediate goalkeeper/support-player resets.
    CHECK(taker == home.players[1]);
    CHECK(home.state.fading_possession_amount == 1.5f);
    CHECK(away.state.fading_possession_amount == .5f);
    CHECK_FALSE(home.players[3]->IsActive());
    CHECK(home.players[3]->GetPosition() == bench_position);
  }
}

TEST_CASE("Half-time kickoff preserves the declared formation exchange condition",
          "[team][kickoff]") {
  const football::pitch::Pitch pitch;
  football::ball::Ball ball({}, pitch);
  for (bool left_owns_ball : {false, true}) {
    KickoffTeam home(0, 1, pitch);
    KickoffTeam away(1, -1, pitch);
    // Distinct layouts expose whether the other team's formation was used.
    away.formation[1].start_position = Vector3(-.4f, .1f, 0);
    away.formation[2].start_position = Vector3(.5f, -.3f, 0);
    MatchOptions options;
    options.left_team_owns_ball = left_owns_ball;
    // Reverse processing order; the awarded side is still identified by ID.
    Player* taker = PositionKickOffPlayers(pitch, away.view, home.view,
        1, 1, ball, football::sim::Seconds(1), options);
    CHECK(PositionKickOffPlayers(pitch, home.view, away.view,
        1, 1, ball, football::sim::Seconds(1), options) == nullptr);
    CheckFormation(home, left_owns_ball ? away : home, pitch);
    CheckFormation(away, left_owns_ball ? home : away, pitch);
    CHECK(taker == away.players[1]);
    CHECK(away.state.fading_possession_amount == 1.5f);
    CHECK(home.state.fading_possession_amount == .5f);
  }
}
