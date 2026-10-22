// P21.4 acceptance: Eliza on-ball actions stay inside the public
// PlayerControl vocabulary, pick a real target, shoot in range, trap/dribble a
// loose ball, tackle a carrier, and degrade safely when the target disappears.
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <string>
#include <utility>

#include "ai/ai_engine.hpp"
#include "ai/eliza/eliza_observation.hpp"
#include "ai/eliza/on_ball.hpp"
#include "football/ai/ai_config.hpp"
#include "football/ai/player_policy.hpp"
#include "football/observation/world_state.hpp"
#include "football/player/player_role.hpp"

namespace {
using football::math::Vector3;
using football::player::PlayerDefinition;
using football::player::PlayerId;
using football::sim::TeamSetup;
using football::sim::TeamSide;

constexpr int kPlayers = 11;

const e_PlayerRole kRoles[kPlayers] = {
    e_PlayerRole_GK, e_PlayerRole_LB, e_PlayerRole_CB, e_PlayerRole_RB,
    e_PlayerRole_DM, e_PlayerRole_CM, e_PlayerRole_LM, e_PlayerRole_RM,
    e_PlayerRole_AM, e_PlayerRole_CF, e_PlayerRole_CF};

TeamSetup MakeTeam(int base_id) {
  TeamSetup team;
  team.name = "team";
  for (int i = 0; i < kPlayers; ++i) {
    PlayerDefinition definition;
    definition.id = static_cast<PlayerId>(base_id + i);
    definition.first_name = "p";
    definition.last_name = std::to_string(i);
    definition.height = 1.8f;
    team.players.push_back(std::move(definition));
    football::team::TacticalFormationEntry entry;
    entry.position.x = -0.85f + 0.17f * static_cast<float>(i);
    entry.position.y = (i % 2 == 0 ? -0.35f : 0.35f);
    entry.role = kRoles[i];
    team.tactical_formation.push_back(entry);
  }
  return team;
}

void AddTeam(WorldState& world, const TeamSetup& team, TeamSide side, float sign) {
  for (int i = 0; i < kPlayers; ++i) {
    WorldPlayerState player;
    player.id = team.players[i].id;
    player.side = side;
    player.position = Vector3(sign * (4.f + 3.f * static_cast<float>(i)),
                              -20.f + 4.f * static_cast<float>(i), 0.f);
    player.facing = Vector3(sign, 0.f, 0.f);
    player.active = true;
    player.max_speed = 8.f;
    player.role = kRoles[i];
    player.fatigue_inv = 1.f;
    player.time_to_ball_ms = 1000;
    player.short_pass_skill = 0.8f;
    player.workrate = 0.8f;
    world.players.push_back(player);
  }
}

WorldState MakeWorld(const TeamSetup& home, const TeamSetup& away) {
  WorldState world;
  world.phase = MatchPhase::FirstHalf;
  world.half_underway = true;
  world.ball_in_play = true;
  world.in_play = true;
  world.ball_position = Vector3(0.f, 0.f, 0.11f);
  world.ball_velocity = Vector3(0.f, 0.f, 0.f);
  world.pitch = football::pitch::Pitch{};
  world.teams = {{{TeamSide::Home, -1, 0}, {TeamSide::Away, 1, 0}}};
  AddTeam(world, home, TeamSide::Home, -1.f);
  AddTeam(world, away, TeamSide::Away, 1.f);
  return world;
}

WorldPlayerState* PlayerOf(WorldState& world, PlayerId id) {
  for (WorldPlayerState& player : world.players)
    if (player.id == id) return &player;
  return nullptr;
}

// A home carrier with a clear lane. `move_all_away` puts every opponent far
// from the pass lane.
WorldState PassScenario(TeamSetup& home, TeamSetup& away, Vector3 carrier,
                        Vector3 target) {
  WorldState world = MakeWorld(home, away);
  world.teams[0].has_possession = true;
  world.teams[0].fading_possession_amount = 1.4f;
  world.ball_retainer = home.players[5].id;
  world.ball_position = carrier;
  for (WorldPlayerState& player : world.players) {
    if (player.side == TeamSide::Home)
      player.position = Vector3(-30.f, -6.f + 1.2f * static_cast<float>(player.id), 0.f);
    else
      player.position = Vector3(40.f, -6.f + 1.2f * static_cast<float>(player.id), 0.f);
  }
  PlayerOf(world, home.players[5].id)->position = carrier;
  PlayerOf(world, home.players[9].id)->position = target;
  return world;
}

}  // namespace

TEST_CASE("Eliza on-ball selects a clear pass target", "[ai]") {
  TeamSetup home = MakeTeam(0);
  TeamSetup away = MakeTeam(11);
  WorldState world = PassScenario(home, away, Vector3(0.f, 0.f, 0.f),
                                  Vector3(20.f, 0.f, 0.f));
  const WorldPlayerState* self = football::ai::eliza::FindPlayer(world, home.players[5].id);
  REQUIRE(self != nullptr);
  const football::ai::eliza::OnBallDecision decision =
      football::ai::eliza::DecideOnBall(world, *self,
                                        football::ai::eliza::kElizaParameters);
  REQUIRE(decision.target_player.has_value());
  CHECK(*decision.target_player == home.players[9].id);
  CHECK((decision.action == ControlAction::ShortPass ||
         decision.action == ControlAction::LongPass ||
         decision.action == ControlAction::HighPass));
}

TEST_CASE("Eliza on-ball degrades safely without a target", "[ai]") {
  TeamSetup home = MakeTeam(0);
  TeamSetup away = MakeTeam(11);
  WorldState world = PassScenario(home, away, Vector3(0.f, 0.f, 0.f),
                                  Vector3(20.f, 0.f, 0.f));
  // Every teammate is gone: no pass and no shot at this range.
  for (WorldPlayerState& player : world.players) {
    if (player.side == TeamSide::Home && player.id != home.players[5].id)
      player.active = false;
  }
  const WorldPlayerState* self = football::ai::eliza::FindPlayer(world, home.players[5].id);
  REQUIRE(self != nullptr);
  const football::ai::eliza::OnBallDecision decision =
      football::ai::eliza::DecideOnBall(world, *self,
                                        football::ai::eliza::kElizaParameters);
  CHECK(decision.action == ControlAction::None);
  CHECK_FALSE(decision.target_player.has_value());
}

TEST_CASE("Eliza on-ball shoots in range", "[ai]") {
  TeamSetup home = MakeTeam(0);
  TeamSetup away = MakeTeam(11);
  WorldState world = PassScenario(home, away, Vector3(45.f, 0.f, 0.f),
                                  Vector3(30.f, 0.f, 0.f));
  const WorldPlayerState* self = football::ai::eliza::FindPlayer(world, home.players[5].id);
  REQUIRE(self != nullptr);
  const football::ai::eliza::OnBallDecision decision =
      football::ai::eliza::DecideOnBall(world, *self,
                                        football::ai::eliza::kElizaParameters);
  CHECK(decision.action == ControlAction::Shoot);
  CHECK(decision.target_position.has_value());
  CHECK(decision.power > 0.0f);
  CHECK(decision.power <= 1.0f);
}

TEST_CASE("Eliza loose ball traps a fast ball and dribbles a slow one", "[ai]") {
  TeamSetup home = MakeTeam(0);
  TeamSetup away = MakeTeam(11);
  WorldState world = MakeWorld(home, away);
  const WorldPlayerState* self = football::ai::eliza::FindPlayer(world, home.players[5].id);
  REQUIRE(self != nullptr);
  WorldPlayerState& actor = *PlayerOf(world, home.players[5].id);
  actor.position = Vector3(0.f, 0.f, 0.f);
  world.ball_position = Vector3(1.f, 0.f, 0.11f);
  world.ball_velocity = Vector3(3.f, 0.f, 0.f);
  CHECK(football::ai::eliza::DecideLooseBall(world, actor,
                                             football::ai::eliza::kElizaParameters).action ==
        ControlAction::Trap);
  world.ball_velocity = Vector3(1.f, 0.f, 0.f);
  CHECK(football::ai::eliza::DecideLooseBall(world, actor,
                                             football::ai::eliza::kElizaParameters).action ==
        ControlAction::Dribble);
  // Too far away: no action.
  world.ball_position = Vector3(6.f, 0.f, 0.11f);
  CHECK(football::ai::eliza::DecideLooseBall(world, actor,
                                             football::ai::eliza::kElizaParameters).action ==
        ControlAction::None);
}

TEST_CASE("Eliza tackles only a close carrier", "[ai]") {
  TeamSetup home = MakeTeam(0);
  TeamSetup away = MakeTeam(11);
  WorldState world = MakeWorld(home, away);
  world.teams[0].has_possession = false;
  world.teams[0].fading_possession_amount = 0.6f;
  world.ball_retainer = away.players[9].id;
  WorldPlayerState& defender = *PlayerOf(world, home.players[4].id);
  WorldPlayerState& carrier = *PlayerOf(world, away.players[9].id);
  defender.position = Vector3(0.f, 0.f, 0.f);
  carrier.position = Vector3(1.5f, 0.f, 0.f);
  world.ball_position = Vector3(0.5f, 0.f, 0.11f);
  CHECK(football::ai::eliza::DecideTackle(world, defender) == ControlAction::Tackle);
  // Out of reach: no tackle.
  carrier.position = Vector3(10.f, 0.f, 0.f);
  world.ball_position = Vector3(9.f, 0.f, 0.11f);
  CHECK(football::ai::eliza::DecideTackle(world, defender) == ControlAction::None);
}

TEST_CASE("Eliza policy only emits protocol actions", "[ai]") {
  TeamSetup home = MakeTeam(0);
  TeamSetup away = MakeTeam(11);
  football::ai::AIConfig config;
  config.player_policy = football::ai::PlayerPolicyKind::Eliza;
  WorldState world = PassScenario(home, away, Vector3(0.f, 0.f, 0.f),
                                  Vector3(20.f, 0.f, 0.f));
  football::ai::AIEngine engine(home, away, football::pitch::Pitch{}, config);
  PlayerControlSet output;
  engine.Update(world, output);
  CHECK(output.controls().size() == static_cast<std::size_t>(kPlayers) * 2);
  for (const PlayerControl& control : output.controls()) {
    CHECK(control.action <= ControlAction::Save);
    CHECK(std::isfinite(control.desired_speed));
    CHECK(control.desired_speed >= 0.0f);
    if (control.target_player) {
      const WorldPlayerState* target =
          football::ai::eliza::FindPlayer(world, *control.target_player);
      const WorldPlayerState* self =
          football::ai::eliza::FindPlayer(world, control.player);
      REQUIRE(target != nullptr);
      REQUIRE(self != nullptr);
      CHECK(target->active);
      CHECK(target->side == self->side);
    }
  }
}
