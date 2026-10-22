// TACT-7: the read-only CoachContext collector. It folds only real observation
// facts, keeps a bounded window and deduplicates derived events.
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <string>

#include "ai/coach/context_collector.hpp"
#include "football/ai/coach_context.hpp"

namespace {

using football::ai::CoachEventType;
using football::ai::ContextCollector;
using football::ai::TacticalBoard;
using football::sim::TeamSide;

constexpr int kPlayers = 11;

football::sim::TeamSetup MakeTeam(int base_id) {
  football::sim::TeamSetup team;
  team.name = "context";
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
    football::team::TacticalFormationEntry entry;
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

WorldState MakeWorld(const football::sim::TeamSetup& home,
                     const football::sim::TeamSetup& away) {
  WorldState world;
  world.tick = 0;
  world.phase = MatchPhase::FirstHalf;
  world.half_underway = true;
  world.in_play = true;
  world.ball_in_play = true;
  world.ball_position = football::math::Vector3(20.f, 0.f, 0.11f);
  world.pitch = Pitch();
  world.teams = {{{TeamSide::Home, -1, 0}, {TeamSide::Away, 1, 0}}};
  auto add = [&](const football::sim::TeamSetup& team, TeamSide side, float sign) {
    for (int i = 0; i < kPlayers; ++i) {
      WorldPlayerState player;
      player.id = team.players[i].id;
      player.side = side;
      player.position = football::math::Vector3(sign * (2.f + 4.f * i), 0.f, 0.f);
      player.facing = football::math::Vector3(sign, 0.f, 0.f);
      player.active = true;
      player.max_speed = 8.f;
      player.role = team.tactical_formation[i].role;
      player.fatigue_inv = 0.9f;
      world.players.push_back(player);
    }
  };
  add(home, TeamSide::Home, -1.f);
  add(away, TeamSide::Away, 1.f);
  return world;
}

}  // namespace

TEST_CASE("the collector folds trends and derives events",
          "[ai][tactical][context]") {
  const auto home = MakeTeam(0);
  const auto away = MakeTeam(100);
  WorldState world = MakeWorld(home, away);
  const std::array<TacticalBoard, 2> boards = {
      football::ai::MakeTacticalBoard(home, TeamSide::Home, Pitch()),
      football::ai::MakeTacticalBoard(away, TeamSide::Away, Pitch())};

  ContextCollector collector(TeamSide::Home);
  std::vector<football::ai::ActiveShout> no_shouts;

  world.tick = 0;
  world.teams[0].has_possession = true;
  collector.OnTick(world, no_shouts);

  world.tick = 500;
  world.teams[0].has_possession = true;
  collector.OnTick(world, no_shouts);

  world.tick = 600;  // within the sample interval: not folded
  collector.OnTick(world, no_shouts);

  world.tick = 1000;
  world.teams[0].has_possession = false;
  collector.OnTick(world, no_shouts);

  world.tick = 2000;
  world.teams[1].score = 1;  // opponent scores
  collector.OnTick(world, no_shouts);

  world.tick = 20000;
  world.restart = e_GameMode_Corner;
  world.in_play = false;
  collector.OnTick(world, no_shouts);

  const football::ai::CoachContext context =
      collector.Build(world, boards[0]);
  CHECK(context.self == TeamSide::Home);
  CHECK(context.observed_tick == 20000);
  CHECK(context.board_revision == boards[0].revision);
  CHECK(context.opponent_score == 1);
  REQUIRE(context.own_players.size() == kPlayers);
  for (const auto& player : context.own_players) CHECK(player.active);
  CHECK(context.current_tactics.in_possession.tempo == 0.5f);

  // 3 samples in the 5-minute window (0, 500, 1000), plus 2000 and 20000 ->
  // the 5-minute window covers the last 30000 ticks, so all 5 sampled ticks.
  CHECK(context.last_5_min.sample_count >= 3);
  CHECK(context.last_5_min.possession_fraction > 0.0f);
  CHECK(context.last_5_min.possession_fraction < 1.0f);

  bool goal_seen = false;
  bool restart_seen = false;
  bool out_of_play_seen = false;
  for (const auto& event : context.recent_events) {
    if (event.type == CoachEventType::Goal) goal_seen = true;
    if (event.type == CoachEventType::Restart) restart_seen = true;
    if (event.type == CoachEventType::BallOutOfPlay) out_of_play_seen = true;
  }
  CHECK(goal_seen);
  CHECK(restart_seen);
  CHECK(out_of_play_seen);
}

TEST_CASE("the collector always exposes the coach's plan",
          "[ai][tactical][context]") {
  const auto home = MakeTeam(0);
  const auto away = MakeTeam(100);
  WorldState world = MakeWorld(home, away);
  const football::ai::TacticalBoard board =
      football::ai::MakeTacticalBoard(home, TeamSide::Home, Pitch());
  ContextCollector collector(TeamSide::Home);
  std::vector<football::ai::ActiveShout> shouts;
  collector.OnTick(world, shouts);
  const auto context = collector.Build(world, board);
  CHECK(context.current_tactics.in_possession.tempo == 0.5f);
}

TEST_CASE("reset clears the collected window", "[ai][tactical][context]") {
  const auto home = MakeTeam(0);
  const auto away = MakeTeam(100);
  WorldState world = MakeWorld(home, away);
  auto board = football::ai::MakeTacticalBoard(home, TeamSide::Home, Pitch());
  ContextCollector collector(TeamSide::Home);
  std::vector<football::ai::ActiveShout> shouts;
  collector.OnTick(world, shouts);
  collector.Reset();
  const auto context = collector.Build(world, board);
  CHECK(context.last_5_min.sample_count == 0);
  CHECK(context.recent_events.empty());
}

TEST_CASE("the collector ingests formal confirmed events exactly once",
          "[ai][tactical][context]") {
  const auto home = MakeTeam(0);
  const auto away = MakeTeam(100);
  WorldState world = MakeWorld(home, away);
  const auto board = football::ai::MakeTacticalBoard(home, TeamSide::Home, Pitch());
  ContextCollector collector(TeamSide::Home);

  football::sim::event::MatchEventRecord card;
  card.sequence = 1;
  football::sim::event::CardShownEvent shown;
  shown.tick = football::sim::Tick{700};
  shown.team = TeamSide::Away;
  shown.player = 100;
  shown.red = true;
  card.event = shown;
  const std::array<football::sim::event::MatchEventRecord, 1> events{card};

  collector.OnTick(world, {}, events);
  const auto context = collector.Build(world, board);
  bool card_seen = false;
  for (const auto& event : context.recent_events) {
    if (event.type == CoachEventType::Card && event.side == TeamSide::Away) {
      card_seen = true;
    }
  }
  CHECK(card_seen);

  // Replaying the same sequence must not duplicate the event.
  collector.OnTick(world, {}, events);
  const auto again = collector.Build(world, board);
  CHECK(again.recent_events.size() == context.recent_events.size());
}
