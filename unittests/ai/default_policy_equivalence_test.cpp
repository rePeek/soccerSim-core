// P21.1 acceptance: the frozen AI protocol must not change the Default policy.
//
// AIEngine(PlayerPolicyKind::Default) and the public DefaultAI baseline must
// produce a field-identical PlayerControlSet for the same world and boards, so
// replacing the whole AI implementation cannot move the Player execution
// boundary. AIEngine is internal, so this target also carries the src/ root.
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>

#include "ai/ai_engine.hpp"
#include "football/ai/ai_config.hpp"
#include "football/ai/default_ai.hpp"
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

TeamSetup MakeTeam(int base_id) {
  TeamSetup team;
  team.name = "team";
  const e_PlayerRole roles[kPlayers] = {
      e_PlayerRole_GK, e_PlayerRole_LB, e_PlayerRole_CB, e_PlayerRole_RB,
      e_PlayerRole_DM, e_PlayerRole_CM, e_PlayerRole_LM, e_PlayerRole_RM,
      e_PlayerRole_AM, e_PlayerRole_CF, e_PlayerRole_CF};
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
    entry.role = roles[i];
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

PlayerControlSet RunBaseline(const TeamSetup& home, const TeamSetup& away,
                             const football::ai::AIConfig& config,
                             const WorldState& world) {
  football::ai::DefaultAI policy(home, away, football::pitch::Pitch{}, config);
  PlayerControlSet output;
  policy.Update(world, output);
  return output;
}

PlayerControlSet RunEngine(const TeamSetup& home, const TeamSetup& away,
                           const football::ai::AIConfig& config,
                           const WorldState& world) {
  football::ai::AIEngine engine(home, away, football::pitch::Pitch{}, config);
  PlayerControlSet output;
  engine.Update(world, output);
  return output;
}

void CheckIdentical(const PlayerControlSet& baseline,
                    const PlayerControlSet& engine) {
  REQUIRE(baseline.controls().size() == engine.controls().size());
  for (std::size_t i = 0; i < baseline.controls().size(); ++i) {
    const PlayerControl& a = baseline.controls()[i];
    const PlayerControl& b = engine.controls()[i];
    INFO("control index " << i);
    CHECK(a.player == b.player);
    CHECK(a.move_direction == b.move_direction);
    CHECK(a.desired_speed == b.desired_speed);
    CHECK(a.look_at == b.look_at);
    CHECK(a.action == b.action);
    CHECK(a.target_player == b.target_player);
    CHECK(a.target_position == b.target_position);
    CHECK(a.power == b.power);
  }
}

}  // namespace

TEST_CASE("AIEngine Default policy is field-identical to DefaultAI", "[ai]") {
  const TeamSetup home = MakeTeam(0);
  const TeamSetup away = MakeTeam(11);
  football::ai::AIConfig config;

  SECTION("open play") {
    const WorldState world = MakeWorld(home, away);
    CheckIdentical(RunBaseline(home, away, config, world),
                   RunEngine(home, away, config, world));
  }
  SECTION("possession and ball motion") {
    WorldState world = MakeWorld(home, away);
    world.players[4].has_possession = true;
    world.ball_retainer = world.players[4].id;
    world.ball_position = Vector3(-15.f, 5.f, 0.11f);
    world.ball_velocity = Vector3(-4.f, 1.f, 0.f);
    CheckIdentical(RunBaseline(home, away, config, world),
                   RunEngine(home, away, config, world));
  }
  SECTION("pending restart") {
    WorldState world = MakeWorld(home, away);
    world.restart_pending = true;
    for (std::size_t i = 0; i < world.players.size(); ++i)
      world.players[i].restart_target = Vector3(0.f, -10.f + 2.f * static_cast<float>(i), 0.f);
    CheckIdentical(RunBaseline(home, away, config, world),
                   RunEngine(home, away, config, world));
  }
  SECTION("set piece with a taker") {
    WorldState world = MakeWorld(home, away);
    world.in_set_piece = true;
    world.restart = e_GameMode_FreeKick;
    world.restart_taker = world.players[5].id;
    CheckIdentical(RunBaseline(home, away, config, world),
                   RunEngine(home, away, config, world));
  }
  SECTION("initial tactics override") {
    football::ai::AIConfig overridden;
    overridden.initial_tactics[0] = football::ai::MakeTacticalBoard(
        home, TeamSide::Home, football::pitch::Pitch{});
    overridden.initial_tactics[1] = football::ai::MakeTacticalBoard(
        away, TeamSide::Away, football::pitch::Pitch{});
    overridden.initial_tactics[0]->plan.in_possession.attacking_width = 0.4f;
    overridden.initial_tactics[0]->plan.out_of_possession.defensive_line = 0.8f;
    const WorldState world = MakeWorld(home, away);
    CheckIdentical(RunBaseline(home, away, overridden, world),
                   RunEngine(home, away, overridden, world));
  }
}

TEST_CASE("AIEngine Default policy is deterministic", "[ai]") {
  const TeamSetup home = MakeTeam(0);
  const TeamSetup away = MakeTeam(11);
  football::ai::AIConfig config;
  const WorldState world = MakeWorld(home, away);
  CheckIdentical(RunEngine(home, away, config, world),
                 RunEngine(home, away, config, world));
}

TEST_CASE("AIEngine selects the requested player policy", "[ai]") {
  const TeamSetup home = MakeTeam(0);
  const TeamSetup away = MakeTeam(11);
  football::ai::AIConfig config;
  CHECK(config.player_policy == football::ai::PlayerPolicyKind::Default);

  football::ai::AIEngine engine(home, away, football::pitch::Pitch{}, config);
  // The coach-owned boards are reachable and start from the declared shape.
  CHECK(engine.tactics(TeamSide::Home).plan.slots.size() == kPlayers);
  CHECK(engine.tactics(TeamSide::Away).plan.slots.size() == kPlayers);
  CHECK(engine.tactics(TeamSide::Home).side == TeamSide::Home);
  CHECK(engine.tactics(TeamSide::Away).side == TeamSide::Away);

  football::ai::AIConfig eliza;
  eliza.player_policy = football::ai::PlayerPolicyKind::Eliza;
  football::ai::AIEngine eliza_engine(home, away, football::pitch::Pitch{}, eliza);
  PlayerControlSet eliza_output;
  eliza_engine.Update(MakeWorld(home, away), eliza_output);
  CHECK(eliza_output.controls().size() == static_cast<std::size_t>(kPlayers) * 2);
}
