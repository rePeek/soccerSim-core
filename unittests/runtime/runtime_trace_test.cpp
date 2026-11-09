// Public-API match regression: no internal access shim.
//
// The same control tape must produce the same observed WorldState and the same
// confirmed event order in two independent Simulations. The trace covers the
// ball, every player, phase/clock/score/possession, restart state and the event
// sequence, so an execution-order or commit-boundary change is visible.
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "football/ball/ball_model.hpp"
#include "football/match/match_config.hpp"
#include "football/observation/world_state.hpp"
#include "football/pitch/pitch.hpp"
#include "football/player/player_control_set.hpp"
#include "runtime/simulation.hpp"

namespace {

using football::math::Vector3;
using namespace football::sim;

TeamSetup TraceTeam(const char* name, football::player::PlayerId first_id) {
  TeamSetup team;
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

void InitTraceMatch(Simulation& sim, TickSpan half = Minutes(45)) {
  MatchOptions options;
  options.snapshot_capacity = 64;
  options.half_duration = half;
  options.ball_position = Vector3(1.25f, -0.75f, 0.0f);
  sim.Init(TraceTeam("home", 0), TraceTeam("away", 11),
           football::pitch::Pitch{}, options, football::ball::BallModel{});
}

std::string EventFingerprint(const event::MatchEvent& match_event) {
  std::ostringstream out;
  out << "ev" << match_event.index() << ':';
  std::visit(
      [&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        out << value.tick.value << ',';
        if constexpr (std::is_same_v<T, event::GoalScoredEvent>) {
          out << static_cast<int>(value.team) << ',' << value.scorer << ','
              << value.own_goal;
        } else if constexpr (std::is_same_v<T, event::CardShownEvent>) {
          out << static_cast<int>(value.team) << ',' << value.player << ','
              << value.red << ',' << value.second_yellow;
        } else if constexpr (std::is_same_v<T, event::RestartAwardedEvent>) {
          out << static_cast<int>(value.team) << ','
              << static_cast<int>(value.kind) << ',' << value.position.coords[0]
              << ',' << value.position.coords[1] << ',' << value.restart_id;
        } else if constexpr (std::is_same_v<T, event::RestartTakenEvent>) {
          out << static_cast<int>(value.team) << ','
              << static_cast<int>(value.kind) << ',' << value.taker << ','
              << value.restart_id;
        } else if constexpr (std::is_same_v<T, event::FoulConfirmedEvent>) {
          out << static_cast<int>(value.team) << ',' << value.offender << ','
              << value.victim;
        } else if constexpr (std::is_same_v<T, event::OffsideConfirmedEvent>) {
          out << static_cast<int>(value.team) << ',' << value.player;
        } else if constexpr (std::is_same_v<T, event::PeriodEndedEvent>) {
          out << static_cast<int>(value.phase);
        } else if constexpr (std::is_same_v<T, event::PassCompletedEvent>) {
          out << static_cast<int>(value.team) << ',' << value.id << ','
              << value.passer << ',' << value.receiver;
        } else if constexpr (std::is_same_v<T, event::ShotEndedEvent>) {
          out << static_cast<int>(value.team) << ',' << value.id << ','
              << value.shooter << ',' << static_cast<int>(value.status) << ','
              << value.goal;
        }
      },
      match_event);
  return out.str();
}

std::string TickTrace(const WorldState& world,
                      const std::vector<event::MatchEventRecord>& events) {
  std::ostringstream out;
  out << std::hexfloat;
  out << "t=" << world.tick << " phase=" << static_cast<int>(world.phase)
      << " clock=" << world.regulation_time.value << " ball_in_play_time="
      << world.ball_in_play_time.value << " underway=" << world.half_underway
      << " in_play=" << world.in_play << " set_piece=" << world.in_set_piece
      << " restart=" << static_cast<int>(world.restart)
      << " restart_pending=" << world.restart_pending
      << " score=" << world.teams[0].score << ':' << world.teams[1].score
      << " poss=" << world.teams[0].has_possession << '/'
      << world.teams[1].has_possession << " dir="
      << world.teams[0].defending_direction << '/'
      << world.teams[1].defending_direction;
  out << " ball=" << world.ball_position.coords[0] << ','
      << world.ball_position.coords[1] << ',' << world.ball_position.coords[2]
      << ';' << world.ball_velocity.coords[0] << ','
      << world.ball_velocity.coords[1] << ',' << world.ball_velocity.coords[2];
  for (const WorldPlayerState& player : world.players) {
    out << " p" << player.id << '=' << player.position.coords[0] << ','
        << player.position.coords[1] << ',' << player.position.coords[2] << ';'
        << player.velocity.coords[0] << ',' << player.velocity.coords[1] << ','
        << player.velocity.coords[2] << ';' << player.facing.coords[0] << ','
        << player.facing.coords[1] << ';' << player.active << ','
        << player.has_possession << ',' << player.time_to_ball_ms;
  }
  for (const event::MatchEventRecord& record : events) {
    out << " #" << record.sequence << ':' << EventFingerprint(record.event);
  }
  return out.str();
}

PlayerControlSet Tape(const WorldState& world, int tick) {
  PlayerControlSet controls;
  for (const WorldPlayerState& player : world.players) {
    if (!player.active) continue;
    const float phase =
        0.07f * static_cast<float>(tick) + 0.35f * static_cast<float>(player.id);
    const Vector3 target =
        player.position + Vector3(2.5f * std::sin(phase),
                                  1.5f * std::cos(phase), 0.0f);
    const int direction =
        world.teams[static_cast<int>(player.side)].defending_direction;
    PlayerControl control;
    control.move_direction =
        (target - player.position)
            .GetNormalized(Vector3(static_cast<float>(-direction), 0.0f, 0.0f));
    control.desired_speed = 4.0f;
    controls.Set(player.id, control);
  }
  return controls;
}

std::vector<std::string> RunTrace(Simulation& sim, int ticks) {
  InitTraceMatch(sim);
  event::MatchEventCursor cursor;
  std::vector<std::string> trace;
  trace.reserve(static_cast<std::size_t>(ticks));
  for (int tick = 0; tick < ticks; ++tick) {
    const WorldState world = sim.Observe();
    sim.Step(Tape(world, tick));
    const WorldState next = sim.Observe();
    trace.push_back(TickTrace(next, sim.ReadEvents(cursor)));
  }
  return trace;
}

}  // namespace

TEST_CASE("Runtime trace: independent matches replay bit-exactly",
          "[runtime][sim][trace]") {
  Simulation first;
  Simulation second;
  const std::vector<std::string> first_trace = RunTrace(first, 120);
  const std::vector<std::string> second_trace = RunTrace(second, 120);
  REQUIRE(first_trace.size() == second_trace.size());

  std::size_t divergence = first_trace.size();
  for (std::size_t i = 0; i < first_trace.size(); ++i) {
    if (first_trace[i] != second_trace[i]) {
      divergence = i;
      break;
    }
  }
  INFO("first divergent tick: " << divergence);
  if (divergence < first_trace.size()) {
    INFO("first:  " << first_trace[divergence]);
    INFO("second: " << second_trace[divergence]);
  }
  CHECK(divergence == first_trace.size());

  if (const char* path = std::getenv("FOOTBALL_TRACE_PATH")) {
    std::ofstream file(path);
    for (const std::string& line : first_trace) file << line << '\n';
  }

  CHECK(first.Stop());
  CHECK(second.Stop());
}

TEST_CASE("Runtime trace: a short match reaches full time and publishes a result",
          "[runtime][sim][lifecycle]") {
  Simulation sim;
  InitTraceMatch(sim, TickSpan{2});
  event::MatchEventCursor cursor;
  int tick = 0;
  while (!sim.Finished() && tick < 2000) {
    const WorldState world = sim.Observe();
    sim.Step(Tape(world, tick++));
    (void)sim.ReadEvents(cursor);
  }
  REQUIRE(sim.Finished());
  CHECK(tick < 2000);
  const MatchResult result = sim.Result();
  CHECK(result.home_score == 0);
  CHECK(result.away_score == 0);
  CHECK(result.outcome == MatchOutcome::Draw);
  CHECK(result.duration_ticks > 0);
  CHECK(sim.Stop());
  CHECK_FALSE(sim.Finished());
}
