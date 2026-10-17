// TACT-3: the Eliza heuristic consumes V2 tactical intent through an explicit
// adapter. Legacy boards keep the historical path; V2 boards add verifiable
// movement and candidate-scoring differences.
#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <optional>
#include <string>

#include "ai/ai_engine.hpp"
#include "ai/eliza/eliza_intent.hpp"
#include "ai/eliza/off_ball.hpp"
#include "football/ai/ai_config.hpp"
#include "football/ai/tactical_interpreter.hpp"

namespace {

using Catch::Approx;
using football::ai::BoardMode;
using football::ai::SlotAssignment;
using football::ai::TacticalBoard;
using football::ai::TacticalInterpreter;
using football::ai::TacticalPlanV2;
using football::ai::eliza::CalculateOffBallTarget;
using football::ai::eliza::ElizaIntent;
using football::ai::eliza::ElizaParameters;
using football::ai::eliza::ElizaScoreWeights;
using football::ai::eliza::MakeElizaIntent;
using football::ai::eliza::MakeElizaScoreWeights;
using football::ai::eliza::MakeElizaTacticalView;
using football::sim::TeamSide;

constexpr int kPlayers = 11;

football::sim::TeamSetup MakeTeam(int base_id) {
  football::sim::TeamSetup team;
  team.name = "tactical";
  constexpr e_PlayerRole kRoles[kPlayers] = {
      e_PlayerRole_GK, e_PlayerRole_LB, e_PlayerRole_CB, e_PlayerRole_RB,
      e_PlayerRole_DM, e_PlayerRole_CM, e_PlayerRole_LM, e_PlayerRole_RM,
      e_PlayerRole_AM, e_PlayerRole_CF, e_PlayerRole_CF};
  for (int i = 0; i < kPlayers; ++i) {
    football::player::PlayerDefinition definition;
    definition.id = static_cast<football::player::PlayerId>(base_id + i);
    definition.first_name = "p";
    definition.last_name = std::to_string(i);
    definition.height = 1.8f;
    team.players.push_back(definition);
    football::sim::TacticalFormationEntry entry;
    entry.position.x = -0.85f + 0.17f * static_cast<float>(i);
    entry.position.y = (i % 2 == 0 ? -0.3f : 0.3f);
    entry.role = kRoles[i];
    team.tactical_formation.push_back(entry);
  }
  return team;
}

const football::pitch::Pitch& Pitch() {
  static const football::pitch::Pitch pitch;
  return pitch;
}

void AddTeam(WorldState& world, const football::sim::TeamSetup& team,
             TeamSide side, float sign) {
  for (int i = 0; i < kPlayers; ++i) {
    WorldPlayerState player;
    player.id = team.players[i].id;
    player.side = side;
    player.position = football::math::Vector3(
        sign * (2.f + 7.f * static_cast<float>(i)),
        -20.f + 4.f * static_cast<float>(i), 0.f);
    player.facing = football::math::Vector3(sign, 0.f, 0.f);
    player.active = true;
    player.max_speed = 8.f;
    player.role = team.tactical_formation[i].role;
    player.fatigue_inv = 1.f;
    player.time_to_ball_ms = 1000;
    player.short_pass_skill = 0.8f;
    player.workrate = 0.8f;
    world.players.push_back(player);
  }
}

WorldState MakeWorld(const football::sim::TeamSetup& home,
                     const football::sim::TeamSetup& away) {
  WorldState world;
  world.phase = MatchPhase::FirstHalf;
  world.half_underway = true;
  world.in_play = true;
  world.ball_in_play = true;
  world.ball_position = football::math::Vector3(0.f, 0.f, 0.11f);
  world.pitch = Pitch();
  world.teams = {{{TeamSide::Home, -1, 0}, {TeamSide::Away, 1, 0}}};
  world.teams[0].has_possession = false;
  world.teams[0].fading_possession_amount = 1.0f;
  AddTeam(world, home, TeamSide::Home, -1.f);
  AddTeam(world, away, TeamSide::Away, 1.f);
  return world;
}

std::array<TacticalBoard, 2> MakeBoards(const football::sim::TeamSetup& home,
                                        const football::sim::TeamSetup& away) {
  std::array<TacticalBoard, 2> boards;
  boards[0] = football::ai::MakeTacticalBoardV2(home, TeamSide::Home, Pitch());
  boards[1] = football::ai::MakeTacticalBoardV2(away, TeamSide::Away, Pitch());
  return boards;
}

SlotAssignment AssignmentFor(const WorldState& world, TeamSide side) {
  return football::ai::DefaultSlotAssignment(world, side);
}

}  // namespace

TEST_CASE("legacy intent keeps the historical score weights",
          "[ai][tactical][response]") {
  const ElizaIntent legacy;  // has_plan == false
  const ElizaScoreWeights weights = MakeElizaScoreWeights(legacy);
  CHECK(weights.shoot_odds_threshold == Approx(0.30f));
  CHECK(weights.pass_release_shift == Approx(0.0f));
  CHECK(weights.long_directness == Approx(0.2f));
}

TEST_CASE("score weights respond monotonically to intent",
          "[ai][tactical][response]") {
  ElizaIntent intent;
  intent.has_plan = true;

  intent.risk_taking = 0.2f;
  const auto cautious = MakeElizaScoreWeights(intent);
  intent.risk_taking = 0.9f;
  const auto aggressive = MakeElizaScoreWeights(intent);
  CHECK(aggressive.shoot_odds_threshold < cautious.shoot_odds_threshold);
  CHECK(aggressive.long_directness > cautious.long_directness);

  intent.tempo = 0.2f;
  const auto slow = MakeElizaScoreWeights(intent);
  intent.tempo = 0.9f;
  const auto fast = MakeElizaScoreWeights(intent);
  CHECK(fast.pass_release_shift > slow.pass_release_shift);

  intent.transition_on_win = -1.0f;  // hold shape
  const auto hold = MakeElizaScoreWeights(intent);
  intent.transition_on_win = 1.0f;  // counter
  const auto counter = MakeElizaScoreWeights(intent);
  CHECK(counter.long_directness > hold.long_directness);
}

TEST_CASE("the interpreted view carries anchors and identity scaling",
          "[ai][tactical][response]") {
  const auto home = MakeTeam(0);
  const auto away = MakeTeam(100);
  const auto boards = MakeBoards(home, away);
  const WorldState world = MakeWorld(home, away);
  std::array<SlotAssignment, 2> assignments = {AssignmentFor(world, TeamSide::Home),
                                               AssignmentFor(world, TeamSide::Away)};
  const auto interpretation = TacticalInterpreter::Build(
      boards, world, assignments,
      football::ai::InterpretationState::FromWorld(world));

  REQUIRE(interpretation.teams[0].has_plan);
  const TacticalBoard view =
      MakeElizaTacticalView(interpretation.teams[0], interpretation.players[0], world,
                            TeamSide::Home);
  CHECK(view.mode == BoardMode::Legacy);
  CHECK(view.width == Approx(0.75f));
  CHECK(view.depth == Approx(0.55f));
  REQUIRE(view.players.size() == kPlayers);
  for (std::size_t i = 0; i < view.players.size(); ++i) {
    REQUIRE(view.players[i].formation_position.has_value());
    CHECK(view.players[i].formation_position->coords[0] ==
          Approx(interpretation.players[0][i].target_anchor_metres.coords[0]));
    CHECK(view.players[i].formation_position->coords[1] ==
          Approx(interpretation.players[0][i].target_anchor_metres.coords[1]));
  }
  const ElizaIntent intent = MakeElizaIntent(interpretation.teams[0]);
  CHECK(intent.has_plan);
  CHECK(intent.width == Approx(interpretation.teams[0].width));
  CHECK(intent.pressing_intensity == Approx(interpretation.teams[0].press));
}

TEST_CASE("pressing intensity scales the defensive engagement",
          "[ai][tactical][response]") {
  const ElizaIntent legacy;
  CHECK(ElizaDefenceBias(0.4f, legacy) == Approx(0.4f));

  ElizaIntent weak;
  weak.has_plan = true;
  weak.pressing_intensity = 0.1f;
  ElizaIntent strong;
  strong.has_plan = true;
  strong.pressing_intensity = 0.9f;
  CHECK(ElizaDefenceBias(0.4f, strong) > ElizaDefenceBias(0.4f, weak));

  // Counter-press lifts it, regroup lowers it, relative to balanced.
  ElizaIntent balanced = strong;
  balanced.transition_on_loss = 0.0f;
  ElizaIntent counter = strong;
  counter.transition_on_loss = 1.0f;
  ElizaIntent regroup = strong;
  regroup.transition_on_loss = -1.0f;
  CHECK(ElizaDefenceBias(0.4f, counter) > ElizaDefenceBias(0.4f, balanced));
  CHECK(ElizaDefenceBias(0.4f, regroup) < ElizaDefenceBias(0.4f, balanced));
}

TEST_CASE("Eliza on V2 boards stays legal and responds to width",
          "[ai][tactical][response]") {
  const auto home = MakeTeam(0);
  const auto away = MakeTeam(100);

  auto run = [&](float attacking_width) {
    auto boards = MakeBoards(home, away);
    boards[0].advanced->in_possession.attacking_width = attacking_width;
    football::ai::AIConfig config;
    config.player_policy = football::ai::PlayerPolicyKind::Eliza;
    config.initial_tactics[0] = boards[0];
    config.initial_tactics[1] = boards[1];
    football::ai::AIEngine engine(home, away, Pitch(), config);
    WorldState world = MakeWorld(home, away);
    // Home keeps the ball so the out-of-possession press is not the driver.
    world.teams[0].has_possession = true;
    world.teams[0].fading_possession_amount = 1.2f;
    world.ball_retainer = home.players[5].id;
    for (auto& player : world.players)
      if (player.id == home.players[5].id) player.has_possession = true;
    PlayerControlSet output;
    engine.Update(world, output);
    return output;
  };

  const PlayerControlSet narrow = run(0.2f);
  const PlayerControlSet wide = run(0.95f);

  // Every active player gets a legal, finite control.
  int controls = 0;
  for (const auto& control : narrow.controls()) {
    ++controls;
    CHECK(std::isfinite(control.move_direction.coords[0]));
    CHECK(std::isfinite(control.move_direction.coords[1]));
    CHECK(control.desired_speed <= 8.0f + 1e-4f);
    CHECK(control.desired_speed >= 0.0f);
  }
  CHECK(controls == 2 * kPlayers);

  // The width instruction changes at least one observed off-ball movement.
  bool differs = false;
  for (const auto& control : narrow.controls()) {
    const auto* other = wide.Get(control.player);
    REQUIRE(other != nullptr);
    if (std::abs(control.move_direction.coords[1] - other->move_direction.coords[1]) >
        1e-4f)
      differs = true;
  }
  CHECK(differs);
}

TEST_CASE("Eliza V2 keeps restarts and keeper behaviour intact",
          "[ai][tactical][response]") {
  const auto home = MakeTeam(0);
  const auto away = MakeTeam(100);
  auto boards = MakeBoards(home, away);
  football::ai::AIConfig config;
  config.player_policy = football::ai::PlayerPolicyKind::Eliza;
  config.initial_tactics[0] = boards[0];
  config.initial_tactics[1] = boards[1];
  football::ai::AIEngine engine(home, away, Pitch(), config);

  WorldState world = MakeWorld(home, away);
  world.restart_pending = true;
  for (auto& player : world.players)
    player.restart_target = player.position + football::math::Vector3(1.f, 0.f, 0.f);
  PlayerControlSet output;
  engine.Update(world, output);
  CHECK(output.controls().size() == 2 * kPlayers);
  for (const auto& control : output.controls()) {
    CHECK(control.action == ControlAction::None);  // never request a ball touch
    CHECK(std::isfinite(control.move_direction.coords[0]));
  }

  // A keeper still produces a legal control when the ball is in play.
  WorldState live = MakeWorld(home, away);
  live.ball_position = football::math::Vector3(-45.f, 0.f, 0.11f);
  live.ball_velocity = football::math::Vector3(-3.f, 0.f, 0.f);
  PlayerControlSet live_output;
  engine.Update(live, live_output);
  const auto* keeper = live_output.Get(home.players[0].id);
  REQUIRE(keeper != nullptr);
  CHECK(std::isfinite(keeper->desired_speed));
  CHECK(keeper->desired_speed <= 8.0f + 1e-4f);
}
