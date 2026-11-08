// Deterministic match trace: the same input tape must produce the same
// per-tick authoritative state and event order in two independent Simulations.
//
// This is the R0 regression anchor. It is deliberately value-only: it records
// the ball, every player, phase/score/possession and the confirmed event
// sequence, so a domain migration that changes execution order or a commit
// boundary shows up as a trace divergence. Set FOOTBALL_TRACE_PATH to also
// write the trace to a file for a manual before/after diff.
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
#include "football/observation/world_state.hpp"
#include "football/pitch/pitch.hpp"
#include "football/player/player_control_set.hpp"
#include "football/match/match_config.hpp"
#include "player/player.hpp"
#include "runtime/simulation.hpp"
#include "testing/simulation_access.hpp"

namespace {

using football::math::Vector3;
using football::sim::testing::SimulationAccess;
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

void InitTraceMatch(Simulation& sim) {
  MatchOptions options;
  options.snapshot_capacity = 32;
  options.ball_position = Vector3(1.25f, -0.75f, 0.0f);
  sim.Init(TraceTeam("home", 0), TraceTeam("away", 11),
           football::pitch::Pitch{}, options, football::ball::BallModel{});
  SimulationAccess::ClearRestart(sim);
  SimulationAccess::SetPhase(sim, MatchPhase::FirstHalf);
  SimulationAccess::StartPlay(sim);
  SimulationAccess::StartBallInPlay(sim);
}

struct TraceActor {
  Player* player = nullptr;
  Vector3 anchor;
  int attack_direction = 1;
};

std::vector<TraceActor> TraceActors(Simulation& sim) {
  std::vector<TraceActor> actors;
  for (int team = 0; team < 2; ++team) {
    const int direction =
        SimulationAccess::TeamOrientationOf(sim, team).attack_direction();
    for (Player* player : SimulationAccess::TeamOf(sim, team).Players()) {
      if (player->IsActive()) {
        actors.push_back({player, player->GetPosition(), direction});
      }
    }
  }
  return actors;
}

PlayerControlSet TraceTape(const std::vector<TraceActor>& actors, int tick) {
  PlayerControlSet controls;
  for (const TraceActor& actor : actors) {
    const float phase =
        0.07f * static_cast<float>(tick) + 0.35f * actor.player->GetID();
    const Vector3 target =
        actor.anchor + Vector3(3.0f * std::sin(phase),
                               2.0f * std::cos(phase), 0.0f);
    PlayerControl control;
    control.move_direction =
        (target - actor.player->GetPosition())
            .GetNormalized(Vector3(static_cast<float>(actor.attack_direction),
                                   0.0f, 0.0f));
    control.desired_speed = 4.5f;
    controls.Set(actor.player->GetID(), control);
  }
  return controls;
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


std::string TickTrace(Simulation& sim) {
  std::ostringstream out;
  out << std::hexfloat;
  out << "t=" << sim.Observe().tick << " phase="
      << static_cast<int>(SimulationAccess::PhaseOf(sim)) << " score="
      << SimulationAccess::ScoreOf(sim, 0) << ':'
      << SimulationAccess::ScoreOf(sim, 1) << " poss="
      << (SimulationAccess::BestPossessionTeamOf(sim).has_value()
              ? static_cast<int>(*SimulationAccess::BestPossessionTeamOf(sim))
              : -1);
  const auto& ball = SimulationAccess::BallOf(sim)->state();
  out << " ball=" << ball.position.coords[0] << ',' << ball.position.coords[1]
      << ',' << ball.position.coords[2] << ';' << ball.velocity.coords[0] << ','
      << ball.velocity.coords[1] << ',' << ball.velocity.coords[2] << ';'
      << ball.angular_velocity.coords[0] << ','
      << ball.angular_velocity.coords[1] << ','
      << ball.angular_velocity.coords[2];
  for (int team = 0; team < 2; ++team) {
    for (Player* player : SimulationAccess::TeamOf(sim, team).Players()) {
      const PlayerKinematicState& state = player->GetKinematicState();
      const auto action = player->GetSimulationActionState();
      out << " p" << player->GetID() << '=' << state.position.coords[0] << ','
          << state.position.coords[1] << ',' << state.position.coords[2] << ';'
          << state.velocity.coords[0] << ',' << state.velocity.coords[1] << ','
          << state.velocity.coords[2] << ';' << state.bodyFacing.coords[0] << ','
          << state.bodyFacing.coords[1] << ',' << state.bodyFacing.coords[2]
          << ';' << static_cast<int>(action.type) << ',' << action.Frame() << ';'
          << player->GetCurrentAnimationId() << ','
          << player->GetCurrentFrame();
    }
  }
  for (const auto& record : SimulationAccess::MatchEventsOf(sim)) {
    out << " #" << record.sequence << ':' << EventFingerprint(record.event);
  }
  return out.str();
}

std::vector<std::string> RunTrace(Simulation& sim, int ticks) {
  InitTraceMatch(sim);
  const std::vector<TraceActor> actors = TraceActors(sim);
  std::vector<std::string> trace;
  trace.reserve(static_cast<std::size_t>(ticks));
  for (int tick = 0; tick < ticks; ++tick) {
    if (tick == 12) SimulationAccess::RequestChangeOfEnds(sim);
    sim.Step(TraceTape(actors, tick));
    trace.push_back(TickTrace(sim));
  }
  return trace;
}

}  // namespace

TEST_CASE("Runtime trace: independent Simulations replay bit-exactly",
          "[runtime][sim][trace]") {
  Simulation first;
  Simulation second;
  const std::vector<std::string> first_trace = RunTrace(first, 40);
  const std::vector<std::string> second_trace = RunTrace(second, 40);
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

TEST_CASE("Runtime trace: a reset does not reuse the previous tick generation",
          "[runtime][sim][trace]") {
  Simulation sim;
  InitTraceMatch(sim);
  const std::uint64_t reset_before = SimulationAccess::ResetSequenceOf(sim);
  SimulationAccess::ResetBall(sim, Vector3(2.0f, 1.0f, 0.0f));
  sim.ResetSituation(Vector3(0.0f, 0.0f, 0.0f));
  CHECK(SimulationAccess::ResetSequenceOf(sim) > reset_before);
  CHECK(sim.Stop());
}
