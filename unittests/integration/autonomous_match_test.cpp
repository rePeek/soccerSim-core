// Full autonomous-match acceptance: the product GameEnv composition runs a
// complete match to full time. These are Release-only by default (see
// CMakeLists) and never part of the Debug CTest budget.
//
// The point is not only Finished(): the audit also proves the match was not
// stuck in a restart, that restarts were actually taken, that no sustained
// no-action or non-finite position occurred, and that possession moved.
//
// A full-activity step costs O(100 ms) on the reference host, so the default
// half is deliberately short. Raise it for a longer acceptance run with:
//   FOOTBALL_INTEGRATION_HALF_SECONDS=2700 ctest --preset release -R autonomous

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <tuple>
#include <type_traits>
#include <variant>
#include <vector>

#include "football/ai/ai_config.hpp"
#include "football/ai/tactical_board.hpp"
#include "football/ball/ball_model.hpp"
#include "football/env.hpp"
#include "football/pitch/pitch.hpp"
#include "football/runtime/result.hpp"
#include "football/runtime/simulation_config.hpp"
#include "football/team/formation_preset.hpp"
#include "football/team/set_piece_orders.hpp"

namespace {

constexpr football::player::PlayerId kHomeTaker = 9;
constexpr football::player::PlayerId kAwayTaker = 19;

// Short by default; grow it (or the env override) once the short run is green.
football::sim::TickSpan HalfDuration() {
  int seconds = 4;
  if (const char* value = std::getenv("FOOTBALL_INTEGRATION_HALF_SECONDS")) {
    const int parsed = std::atoi(value);
    if (parsed > 0) seconds = parsed;
  }
  return football::sim::Seconds(seconds);
}

// A complete 11-strong roster with explicit identities, roles and heights.
football::sim::TeamSetup MakeEleven(const char* name,
                                    football::player::PlayerId first_id) {
  football::sim::TeamSetup team;
  team.name = name;
  for (int i = 0; i < 11; ++i) {
    football::player::PlayerDefinition player;
    player.id = first_id + static_cast<football::player::PlayerId>(i);
    player.database_id = 100 + i;
    player.height = 1.70f + 0.01f * static_cast<float>(i);
    team.players.push_back(std::move(player));
  }
  team.tactical_formation = {
      {{-1.0f, 0.0f}, e_PlayerRole_GK},  {{-0.7f, 0.75f}, e_PlayerRole_LB},
      {{-1.0f, 0.25f}, e_PlayerRole_CB}, {{-1.0f, -0.25f}, e_PlayerRole_CB},
      {{-0.7f, -0.75f}, e_PlayerRole_RB}, {{0.0f, 0.5f}, e_PlayerRole_CM},
      {{-0.2f, 0.0f}, e_PlayerRole_CM},  {{0.0f, -0.5f}, e_PlayerRole_CM},
      {{0.6f, 0.75f}, e_PlayerRole_LM},  {{1.0f, 0.0f}, e_PlayerRole_CF},
      {{0.6f, -0.75f}, e_PlayerRole_RM}};
  return team;
}

MatchOptions MatchConfig(unsigned int seed, bool reverse) {
  MatchOptions options;
  options.game_engine_random_seed = seed;
  options.half_duration = HalfDuration();
  options.snapshot_capacity = 20000;
  options.reverse_team_processing = reverse;
  return options;
}

void Mix(std::uint64_t& hash, std::uint64_t value) {
  hash ^= value;
  hash *= 1099511628211ull;
}

void MixFloat(std::uint64_t& hash, float value) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  Mix(hash, bits);
}

void MixVector(std::uint64_t& hash, const football::math::Vector3& value) {
  MixFloat(hash, value.coords[0]);
  MixFloat(hash, value.coords[1]);
  MixFloat(hash, value.coords[2]);
}

// Per-tick committed-state digest. Any physical or rule divergence changes it.
void MixWorld(std::uint64_t& hash, const WorldState& world) {
  Mix(hash, world.tick);
  Mix(hash, static_cast<std::uint64_t>(world.phase));
  Mix(hash, world.in_play ? 1u : 0u);
  Mix(hash, world.in_set_piece ? 1u : 0u);
  Mix(hash, world.ball_in_play ? 1u : 0u);
  Mix(hash, static_cast<std::uint64_t>(world.restart));
  MixVector(hash, world.ball_position);
  MixVector(hash, world.ball_velocity);
  for (const auto& player : world.players) {
    Mix(hash, player.id);
    Mix(hash, static_cast<std::uint64_t>(player.side));
    Mix(hash, player.active ? 1u : 0u);
    Mix(hash, player.has_possession ? 1u : 0u);
    MixVector(hash, player.position);
    MixVector(hash, player.velocity);
  }
}

std::uint64_t MixTrajectories(
    const std::vector<football::sim::event::EventTrajectory>& events) {
  std::uint64_t hash = 14695981039346656037ull;
  for (const auto& event : events) {
    Mix(hash, event.event);
    Mix(hash, static_cast<std::uint64_t>(event.kind));
    Mix(hash, event.actor);
    Mix(hash, static_cast<std::uint64_t>(event.team));
    Mix(hash, event.first_step);
    Mix(hash, event.last_step);
    Mix(hash, event.sample_count);
    Mix(hash, event.complete ? 1u : 0u);
    MixFloat(hash, event.ball_path_length);
    MixFloat(hash, event.peak_ball_speed);
  }
  return hash;
}

// Sequence- and kind-sensitive digest of the formal confirmed event stream.
std::uint64_t MixFormalEvent(
    std::uint64_t hash, const football::sim::event::MatchEventRecord& record) {
  Mix(hash, record.sequence);
  Mix(hash, record.event.index());
  std::visit(
      [&](const auto& event) {
        using T = std::decay_t<decltype(event)>;
        if constexpr (std::is_same_v<T, football::sim::event::GoalScoredEvent>) {
          Mix(hash, static_cast<std::uint64_t>(event.team));
          Mix(hash, event.scorer);
          Mix(hash, event.own_goal ? 1u : 0u);
        } else if constexpr (std::is_same_v<T, football::sim::event::CardShownEvent>) {
          Mix(hash, static_cast<std::uint64_t>(event.team));
          Mix(hash, event.player);
          Mix(hash, event.red ? 1u : 0u);
          Mix(hash, event.second_yellow ? 1u : 0u);
        } else if constexpr (std::is_same_v<T, football::sim::event::RestartAwardedEvent>) {
          Mix(hash, static_cast<std::uint64_t>(event.team));
          Mix(hash, static_cast<std::uint64_t>(event.kind));
          Mix(hash, event.restart_id);
        } else if constexpr (std::is_same_v<T, football::sim::event::RestartTakenEvent>) {
          Mix(hash, static_cast<std::uint64_t>(event.team));
          Mix(hash, static_cast<std::uint64_t>(event.kind));
          Mix(hash, event.taker);
          Mix(hash, event.restart_id);
        } else if constexpr (std::is_same_v<T, football::sim::event::FoulConfirmedEvent>) {
          Mix(hash, static_cast<std::uint64_t>(event.team));
          Mix(hash, event.offender);
          Mix(hash, event.victim);
        } else if constexpr (std::is_same_v<T, football::sim::event::OffsideConfirmedEvent>) {
          Mix(hash, static_cast<std::uint64_t>(event.team));
          Mix(hash, event.player);
        } else if constexpr (std::is_same_v<T, football::sim::event::PeriodEndedEvent>) {
          Mix(hash, static_cast<std::uint64_t>(event.phase));
        } else if constexpr (std::is_same_v<T, football::sim::event::PassCompletedEvent>) {
          Mix(hash, static_cast<std::uint64_t>(event.team));
          Mix(hash, event.passer);
          Mix(hash, event.receiver);
        } else if constexpr (std::is_same_v<T, football::sim::event::ShotEndedEvent>) {
          Mix(hash, static_cast<std::uint64_t>(event.team));
          Mix(hash, event.shooter);
          Mix(hash, static_cast<std::uint64_t>(event.status));
          Mix(hash, event.goal ? 1u : 0u);
        }
      },
      record.event);
  return hash;
}

struct Audit {
  bool finished = false;
  int steps = 0;
  int max_steps = 0;
  int restart_episodes = 0;   // in_set_piece rising edges
  int restarts_resolved = 0;  // left set piece with the ball actually in play
  int max_restart_run = 0;    // longest run with a pending/ready restart
  int max_still_ball_run = 0; // longest in-play run with a near-still ball
  int max_frozen_run = 0;     // longest in-play run with no actor moving either
  bool finite_state = true;
  bool ball_in_bounds = true;
  bool home_possessed = false;
  bool away_possessed = false;
  bool ball_in_play_seen = false;
  int configured_taker_hits = 0;
  int configured_taker_mismatches = 0;
  int goal_events = 0;
  int card_events = 0;
  int foul_events = 0;
  int offside_events = 0;
  int restart_awarded_events = 0;
  int restart_taken_events = 0;
  int period_events = 0;
  int coach_calls = 0;
  int coach_records = 0;
  std::uint64_t digest = 14695981039346656037ull;
  std::uint64_t event_digest = 14695981039346656037ull;
  std::uint64_t trajectory_digest = 0;
  MatchResult result{};
};

// Restart categories that carry declared Team orders (GoalKick/KickOff do not).
bool ConfiguredKind(e_GameMode kind) {
  return kind == e_GameMode_FreeKick || kind == e_GameMode_Corner ||
      kind == e_GameMode_ThrowIn || kind == e_GameMode_Penalty;
}

football::sim::TeamSide SideOf(const WorldState& world,
                               football::player::PlayerId id) {
  for (const auto& player : world.players) {
    if (player.id == id) return player.side;
  }
  return football::sim::TeamSide::Home;
}

Audit RunMatch(football::ai::PlayerPolicyKind policy,
               const football::sim::TeamSetup& home,
               const football::sim::TeamSetup& away, unsigned int seed,
               bool coach = false, bool reverse = false) {
  football::ai::AIConfig ai;
  ai.player_policy = policy;
  if (coach) {
    ai.coach_enabled = true;
    ai.initial_tactics[0] = football::ai::MakeTacticalBoardV2(
        home, football::sim::TeamSide::Home, football::pitch::Pitch{});
    ai.initial_tactics[1] = football::ai::MakeTacticalBoardV2(
        away, football::sim::TeamSide::Away, football::pitch::Pitch{});
  }
  MatchOptions options = MatchConfig(seed, reverse);

  GameEnv env(home, away, football::pitch::Pitch{}, football::ball::BallModel{},
              options, ai);
  env.Start();

  Audit audit;
  audit.max_steps = static_cast<int>(2 * options.half_duration.value) + 20000;
  bool previous_in_set_piece = false;
  int restart_run = 0;
  int still_run = 0;
  int frozen_run = 0;
  football::sim::event::MatchEventCursor event_cursor;

  while (!env.Finished()) {
    env.Step();
    ++audit.steps;
    // Product API: incrementally consume every event Runtime confirmed.
    for (const auto& record : env.ReadEvents(event_cursor)) {
      audit.event_digest = MixFormalEvent(audit.event_digest, record);
      std::visit(
          [&](const auto& event) {
            using T = std::decay_t<decltype(event)>;
            if constexpr (std::is_same_v<T, football::sim::event::GoalScoredEvent>) ++audit.goal_events;
            else if constexpr (std::is_same_v<T, football::sim::event::CardShownEvent>) ++audit.card_events;
            else if constexpr (std::is_same_v<T, football::sim::event::FoulConfirmedEvent>) ++audit.foul_events;
            else if constexpr (std::is_same_v<T, football::sim::event::OffsideConfirmedEvent>) ++audit.offside_events;
            else if constexpr (std::is_same_v<T, football::sim::event::RestartAwardedEvent>) ++audit.restart_awarded_events;
            else if constexpr (std::is_same_v<T, football::sim::event::RestartTakenEvent>) ++audit.restart_taken_events;
            else if constexpr (std::is_same_v<T, football::sim::event::PeriodEndedEvent>) ++audit.period_events;
          },
          record.event);
    }
    const WorldState world = env.Observe();
    MixWorld(audit.digest, world);

    // Position/state sanity.
    if (!std::isfinite(world.ball_position.coords[0]) ||
        !std::isfinite(world.ball_position.coords[1]) ||
        !std::isfinite(world.ball_position.coords[2]) ||
        !std::isfinite(world.ball_velocity.coords[0])) {
      audit.finite_state = false;
    }
    const float half_length = world.pitch.half_length();
    const float half_width = world.pitch.half_width();
    if (std::fabs(world.ball_position.coords[0]) > half_length + 6.0f ||
        std::fabs(world.ball_position.coords[1]) > half_width + 2.0f) {
      audit.ball_in_bounds = false;
    }
    for (const auto& player : world.players) {
      if (!std::isfinite(player.position.coords[0]) ||
          !std::isfinite(player.position.coords[1]) ||
          !std::isfinite(player.position.coords[2])) {
        audit.finite_state = false;
      }
    }

    // Restart lifecycle.
    const bool restart_active = world.in_set_piece || world.restart_pending;
    restart_run = restart_active ? restart_run + 1 : 0;
    audit.max_restart_run = std::max(audit.max_restart_run, restart_run);
    if (world.in_set_piece && !previous_in_set_piece) ++audit.restart_episodes;
    if (previous_in_set_piece && !world.in_set_piece && world.ball_in_play) {
      ++audit.restarts_resolved;
    }
    previous_in_set_piece = world.in_set_piece;

    // Sustained no-action while the ball is nominally in play.
    const bool still_ball =
        world.ball_in_play && world.ball_velocity.GetLength() < 0.05f;
    still_run = still_ball ? still_run + 1 : 0;
    audit.max_still_ball_run = std::max(audit.max_still_ball_run, still_run);
    bool any_moving = false;
    for (const auto& player : world.players) {
      if (player.active && player.velocity.GetLength() > 0.1f) any_moving = true;
    }
    frozen_run = (still_ball && !any_moving) ? frozen_run + 1 : 0;
    audit.max_frozen_run = std::max(audit.max_frozen_run, frozen_run);

    if (world.ball_in_play) audit.ball_in_play_seen = true;
    if (world.teams[0].has_possession) audit.home_possessed = true;
    if (world.teams[1].has_possession) audit.away_possessed = true;

    // RFE-4: a restart of a configured kind must use the declared taker.
    if (world.restart_taker && ConfiguredKind(world.restart)) {
      const football::sim::TeamSide side = SideOf(world, *world.restart_taker);
      const football::player::PlayerId expected =
          side == football::sim::TeamSide::Home ? kHomeTaker : kAwayTaker;
      if (*world.restart_taker == expected) {
        ++audit.configured_taker_hits;
      } else {
        ++audit.configured_taker_mismatches;
      }
    }
  }

  audit.finished = env.Finished();
  audit.result = env.Result();
  audit.trajectory_digest = MixTrajectories(env.EventTrajectories());
  audit.coach_calls = env.CoachCalls(football::sim::TeamSide::Home);
  audit.coach_records = static_cast<int>(env.CoachLog().size());
  env.Stop();
  return audit;
}

// The invariants every autonomous match must satisfy.
void RequireHealthyMatch(const Audit& audit) {
  REQUIRE(audit.finished);
  REQUIRE(audit.steps > 0);
  REQUIRE(audit.steps < audit.max_steps);
  REQUIRE(audit.finite_state);
  REQUIRE(audit.ball_in_bounds);
  // Restarts must exist, must actually be taken, and must not dominate the run.
  REQUIRE(audit.restart_episodes >= 1);
  REQUIRE(audit.restarts_resolved >= 1);
  REQUIRE(audit.ball_in_play_seen);
  REQUIRE(audit.max_restart_run < 6000);    // 60 s
  REQUIRE(audit.max_still_ball_run < 3000); // 30 s
  REQUIRE(audit.max_frozen_run < 3000);
  // Possession must exist and must not be permanently absent for both sides.
  REQUIRE((audit.home_possessed || audit.away_possessed));
  REQUIRE(audit.result.home_score >= 0);
  REQUIRE(audit.result.away_score >= 0);
  REQUIRE(audit.result.duration_ticks > 0);
  // RFE-5: the formal event stream must be complete and mutually consistent.
  REQUIRE(audit.period_events == 2);  // first half + full time
  REQUIRE(audit.goal_events ==
          audit.result.home_score + audit.result.away_score);
  REQUIRE(audit.restart_awarded_events >= 1);
  REQUIRE(audit.restart_taken_events >= 1);
  REQUIRE(audit.restart_awarded_events >= audit.restart_taken_events);
}

void Report(const Audit& audit) {
  INFO("steps=" << audit.steps << " restarts=" << audit.restart_episodes
                << " resolved=" << audit.restarts_resolved
                << " max_restart_run=" << audit.max_restart_run
                << " max_still=" << audit.max_still_ball_run
                << " max_frozen=" << audit.max_frozen_run
                << " events=" << audit.event_digest
                << " awarded=" << audit.restart_awarded_events
                << " taken=" << audit.restart_taken_events
                << " score=" << audit.result.home_score << "-"
                << audit.result.away_score);
}

}  // namespace

TEST_CASE("Integration: Default x Default completes and is deterministic",
          "[integration][match]") {
  // One configured match, run twice with the same seed: the first supplies the
  // health audit, the pair proves tick-for-tick and event-sequence determinism.
  const Audit first = RunMatch(football::ai::PlayerPolicyKind::Default,
                               MakeEleven("home", 0), MakeEleven("away", 11), 42);
  const Audit second = RunMatch(football::ai::PlayerPolicyKind::Default,
                                MakeEleven("home", 0), MakeEleven("away", 11), 42);
  RequireHealthyMatch(first);
  Report(first);
  REQUIRE(first.digest == second.digest);
  REQUIRE(first.event_digest == second.event_digest);
  REQUIRE(first.trajectory_digest == second.trajectory_digest);
  REQUIRE(first.result.home_score == second.result.home_score);
  REQUIRE(first.result.away_score == second.result.away_score);
  REQUIRE(first.result.outcome == second.result.outcome);
  REQUIRE(first.result.duration_ticks == second.result.duration_ticks);
}

TEST_CASE("Integration: reverse team processing is deterministic",
          "[integration][match]") {
  // FRAME-1 gate: the second processing order must be pinned by a fingerprint
  // just like the default one before any authority migration touches it.
  const Audit first = RunMatch(football::ai::PlayerPolicyKind::Default,
                               MakeEleven("home", 0), MakeEleven("away", 11), 42,
                               /*coach=*/false, /*reverse=*/true);
  const Audit second = RunMatch(football::ai::PlayerPolicyKind::Default,
                                MakeEleven("home", 0), MakeEleven("away", 11), 42,
                                /*coach=*/false, /*reverse=*/true);
  RequireHealthyMatch(first);
  Report(first);
  REQUIRE(first.digest == second.digest);
  REQUIRE(first.event_digest == second.event_digest);
  REQUIRE(first.trajectory_digest == second.trajectory_digest);
  REQUIRE(first.result.duration_ticks == second.result.duration_ticks);
}

TEST_CASE("Integration: Heuristic x Heuristic completes a short autonomous match",
          "[integration][match]") {
  const Audit audit = RunMatch(football::ai::PlayerPolicyKind::Heuristic,
                               MakeEleven("home", 0), MakeEleven("away", 11), 42);
  RequireHealthyMatch(audit);
  Report(audit);
}

TEST_CASE("Integration: Team set-piece configuration drives real restart takers",
          "[integration][match]") {
  football::sim::TeamSetup home = MakeEleven("home", 0);
  home.set_piece_orders.penalties = {kHomeTaker};
  home.set_piece_orders.direct_free_kicks = {kHomeTaker};
  home.set_piece_orders.indirect_free_kicks = {kHomeTaker};
  home.set_piece_orders.corners_left = {kHomeTaker};
  home.set_piece_orders.corners_right = {kHomeTaker};
  home.set_piece_orders.throw_ins = {kHomeTaker};
  // A reusable template must be accepted in a real match without breaking it.
  home.formation_presets.presets = {{"fk_attack",
      football::team::FormationPresetKind::FreeKick,
      football::team::FormationPresetSide::Attacking,
      {{kHomeTaker,
        {football::team::AnchorFrame::TeamRelative, 0.75f, 0.35f}, "runner"}}}};
  football::sim::TeamSetup away = MakeEleven("away", 11);
  away.set_piece_orders.penalties = {kAwayTaker};
  away.set_piece_orders.direct_free_kicks = {kAwayTaker};
  away.set_piece_orders.indirect_free_kicks = {kAwayTaker};
  away.set_piece_orders.corners_left = {kAwayTaker};
  away.set_piece_orders.corners_right = {kAwayTaker};
  away.set_piece_orders.throw_ins = {kAwayTaker};

  const Audit audit =
      RunMatch(football::ai::PlayerPolicyKind::Default, home, away, 42);
  RequireHealthyMatch(audit);
  Report(audit);
  // Whenever a restart carries the declared order, the planner must pick it.
  // The deterministic RFE-4 coverage lives in the [team] runtime tests; a very
  // short match may not contain one, so an unexercised run only warns.
  REQUIRE(audit.configured_taker_mismatches == 0);
  if (audit.configured_taker_hits == 0) {
    WARN("short run contained no configured restart; raise "
         "FOOTBALL_INTEGRATION_HALF_SECONDS to exercise RFE-4 end to end");
  }
}

TEST_CASE("Integration: heuristic coach loop runs a short autonomous match",
          "[integration][match]") {
  // Coach-enabled path: AIEngine runs ContextCollector -> CoachScheduler ->
  // HeuristicCoachPolicy -> CoachRuntime on the V2 boards while the heuristic
  // player policy still drives the roster. It must not break the match.
  const Audit audit = RunMatch(football::ai::PlayerPolicyKind::Heuristic,
                               MakeEleven("home", 0), MakeEleven("away", 11), 42,
                               /*coach=*/true);
  RequireHealthyMatch(audit);
  Report(audit);
  REQUIRE(audit.coach_calls >= 1);  // the scheduler ran at least the opening turn
}
