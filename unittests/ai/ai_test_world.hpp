#ifndef FOOTBALL_AI_TEST_WORLD_HPP
#define FOOTBALL_AI_TEST_WORLD_HPP

#include <string>
#include <utility>

#include "football/observation/world_state.hpp"
#include "football/player/state/player_role.hpp"

namespace football::ai::test {

// Shared synthetic 11v11 observation for the AI tests.
inline constexpr int kPlayers = 11;

inline constexpr e_PlayerRole kRoles[kPlayers] = {
    e_PlayerRole_GK, e_PlayerRole_LB, e_PlayerRole_CB, e_PlayerRole_RB,
    e_PlayerRole_DM, e_PlayerRole_CM, e_PlayerRole_LM, e_PlayerRole_RM,
    e_PlayerRole_AM, e_PlayerRole_CF, e_PlayerRole_CF};

inline football::sim::TeamSetup MakeTeam(int base_id) {
  football::sim::TeamSetup team;
  team.name = "team";
  for (int i = 0; i < kPlayers; ++i) {
    football::player::PlayerDefinition definition;
    definition.id = static_cast<football::player::PlayerId>(base_id + i);
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

inline void AddTeam(WorldState& world, const football::sim::TeamSetup& team,
                    football::sim::TeamSide side, float sign) {
  for (int i = 0; i < kPlayers; ++i) {
    WorldPlayerState player;
    player.id = team.players[i].id;
    player.side = side;
    player.position = football::math::Vector3(
        sign * (4.f + 3.f * static_cast<float>(i)),
        -20.f + 4.f * static_cast<float>(i), 0.f);
    player.facing = football::math::Vector3(sign, 0.f, 0.f);
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

inline WorldState MakeWorld(const football::sim::TeamSetup& home,
                            const football::sim::TeamSetup& away) {
  WorldState world;
  world.phase = MatchPhase::FirstHalf;
  world.half_underway = true;
  world.ball_in_play = true;
  world.in_play = true;
  world.ball_position = football::math::Vector3(0.f, 0.f, 0.11f);
  world.ball_velocity = football::math::Vector3(0.f, 0.f, 0.f);
  world.pitch = football::pitch::Pitch{};
  world.teams = {{{football::sim::TeamSide::Home, -1, 0},
                  {football::sim::TeamSide::Away, 1, 0}}};
  AddTeam(world, home, football::sim::TeamSide::Home, -1.f);
  AddTeam(world, away, football::sim::TeamSide::Away, 1.f);
  return world;
}

}  // namespace football::ai::test

#endif  // FOOTBALL_AI_TEST_WORLD_HPP
