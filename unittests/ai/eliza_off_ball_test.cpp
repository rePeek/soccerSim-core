// P21.3 acceptance: Eliza off-ball behaviour is role-shaped, deterministic and
// safe on degenerate observations. The pure calculation is exercised directly
// and the full policy through AIEngine.
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <string>
#include <utility>

#include "ai/ai_engine.hpp"
#include "ai/eliza/eliza_observation.hpp"
#include "ai/eliza/off_ball.hpp"
#include "football/ai/ai_config.hpp"
#include "football/ai/player_policy.hpp"
#include "football/ai/tactical_board.hpp"
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
    football::sim::TacticalFormationEntry entry;
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

const football::ai::PlayerDirective* DirectiveOf(const football::ai::TacticalBoard& board,
                                                 PlayerId id) {
  for (const football::ai::PlayerDirective& directive : board.players)
    if (directive.player == id) return &directive;
  return nullptr;
}

Vector3 OffBall(const WorldState& world, const football::ai::TacticalBoard& board,
                PlayerId id) {
  const WorldPlayerState* player = football::ai::eliza::FindPlayer(world, id);
  REQUIRE(player != nullptr);
  return football::ai::eliza::CalculateOffBallTarget(
      world, board, DirectiveOf(board, id), *player,
      football::ai::eliza::kElizaParameters);
}

bool IsFinite(const Vector3& v) {
  return std::isfinite(v.coords[0]) && std::isfinite(v.coords[1]) &&
         std::isfinite(v.coords[2]);
}

PlayerControlSet RunEliza(const TeamSetup& home, const TeamSetup& away,
                          const football::ai::AIConfig& config,
                          const WorldState& world) {
  football::ai::AIEngine engine(home, away, football::pitch::Pitch{}, config);
  PlayerControlSet output;
  engine.Update(world, output);
  return output;
}

void CheckSame(const PlayerControlSet& a, const PlayerControlSet& b) {
  REQUIRE(a.controls().size() == b.controls().size());
  for (std::size_t i = 0; i < a.controls().size(); ++i) {
    CHECK(a.controls()[i].player == b.controls()[i].player);
    CHECK(a.controls()[i].move_direction == b.controls()[i].move_direction);
    CHECK(a.controls()[i].desired_speed == b.controls()[i].desired_speed);
    CHECK(a.controls()[i].action == b.controls()[i].action);
  }
}

}  // namespace

TEST_CASE("Eliza off-ball defenders hold a goal-side block", "[ai]") {
  const TeamSetup home = MakeTeam(0);
  const TeamSetup away = MakeTeam(11);
  WorldState world = MakeWorld(home, away);
  // Away attacks the home goal; the home block must stay goal-side of the ball.
  world.ball_position = Vector3(-20.f, 0.f, 0.11f);
  world.ball_retainer = away.players[9].id;
  world.teams[1].has_possession = true;
  world.teams[1].fading_possession_amount = 1.4f;
  world.teams[0].fading_possession_amount = 0.6f;
  for (WorldPlayerState& player : world.players) {
    if (player.id == away.players[9].id) player.position = Vector3(-24.f, 0.f, 0.f);
  }
  const football::pitch::Pitch pitch;
  const football::ai::TacticalBoard board =
      football::ai::MakeTacticalBoard(home, TeamSide::Home, pitch);

  const Vector3 centre_back = OffBall(world, board, home.players[2].id);
  const Vector3 full_back = OffBall(world, board, home.players[1].id);
  CHECK(IsFinite(centre_back));
  CHECK(IsFinite(full_back));
  // Home defends negative x, so goal-side is a smaller x than the ball.
  CHECK(centre_back.coords[0] < world.ball_position.coords[0]);
  CHECK(full_back.coords[0] < world.ball_position.coords[0]);
  CHECK(std::fabs(centre_back.coords[1]) <= world.pitch.half_width());
}

TEST_CASE("Eliza off-ball spreads support instead of clustering", "[ai]") {
  const TeamSetup home = MakeTeam(0);
  const TeamSetup away = MakeTeam(11);
  WorldState world = MakeWorld(home, away);
  world.ball_position = Vector3(0.f, 0.f, 0.11f);
  world.ball_retainer = home.players[5].id;
  world.teams[0].has_possession = true;
  world.teams[0].fading_possession_amount = 1.4f;
  const football::pitch::Pitch pitch;
  const football::ai::TacticalBoard board =
      football::ai::MakeTacticalBoard(home, TeamSide::Home, pitch);

  const int midfielders[4] = {4, 5, 6, 7};
  Vector3 targets[4];
  for (int i = 0; i < 4; ++i)
    targets[i] = OffBall(world, board, home.players[midfielders[i]].id);
  for (int i = 0; i < 4; ++i) {
    CHECK(IsFinite(targets[i]));
    CHECK((targets[i] - world.ball_position).Get2D().GetLength() > 1.0f);
    for (int j = i + 1; j < 4; ++j)
      CHECK((targets[i] - targets[j]).Get2D().GetLength() > 1.0f);
  }
}

TEST_CASE("Eliza off-ball forwards push up while defenders hold", "[ai]") {
  const TeamSetup home = MakeTeam(0);
  const TeamSetup away = MakeTeam(11);
  WorldState world = MakeWorld(home, away);
  world.ball_position = Vector3(0.f, 0.f, 0.11f);
  world.ball_retainer = home.players[5].id;
  world.teams[0].has_possession = true;
  world.teams[0].fading_possession_amount = 1.4f;
  const football::pitch::Pitch pitch;
  const football::ai::TacticalBoard board =
      football::ai::MakeTacticalBoard(home, TeamSide::Home, pitch);

  const Vector3 forward = OffBall(world, board, home.players[9].id);
  const Vector3 centre_back = OffBall(world, board, home.players[2].id);
  CHECK(IsFinite(forward));
  CHECK(IsFinite(centre_back));
  // Home attacks positive x.
  CHECK(forward.coords[0] > centre_back.coords[0] + 5.0f);
}

TEST_CASE("Eliza policy is deterministic", "[ai]") {
  const TeamSetup home = MakeTeam(0);
  const TeamSetup away = MakeTeam(11);
  football::ai::AIConfig config;
  config.player_policy = football::ai::PlayerPolicyKind::Eliza;
  const WorldState world = MakeWorld(home, away);
  CheckSame(RunEliza(home, away, config, world), RunEliza(home, away, config, world));
}

TEST_CASE("Eliza policy is safe on degenerate observations", "[ai]") {
  const TeamSetup home = MakeTeam(0);
  const TeamSetup away = MakeTeam(11);
  football::ai::AIConfig config;
  config.player_policy = football::ai::PlayerPolicyKind::Eliza;
  WorldState world = MakeWorld(home, away);
  // Overlapping players, an inactive player and a moving ball.
  for (WorldPlayerState& player : world.players) player.position = Vector3(0.f, 0.f, 0.f);
  world.players[0].active = false;
  world.ball_position = Vector3(30.f, 10.f, 0.5f);
  world.ball_velocity = Vector3(-12.f, 3.f, 1.f);

  const PlayerControlSet output = RunEliza(home, away, config, world);
  CHECK(output.controls().size() == 21);  // the inactive player is skipped
  for (const PlayerControl& control : output.controls()) {
    CHECK(IsFinite(control.move_direction));
    CHECK(std::isfinite(control.desired_speed));
    CHECK(control.desired_speed >= 0.0f);
    CHECK(control.desired_speed <= 8.0f);
    CHECK(control.action == ControlAction::None);
  }
  CHECK(output.Get(world.players[0].id) == nullptr);
}
