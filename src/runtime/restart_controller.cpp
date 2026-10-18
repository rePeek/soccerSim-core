#include "runtime/restart_controller.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "referee/ruling.hpp"
#include "football/ball/ball.hpp"
#include "player/player.hpp"
#include "referee/rule_command_sink.hpp"
#include "runtime/restart_placement.hpp"
#include "runtime/restart_planner.hpp"

namespace football::sim {
namespace {
using football::math::Vector3;
using football::sim::Seconds;
using football::sim::Tick;
using football::sim::TickSpan;
using football::sim::RuntimeTickFacts;
using football::sim::rules::RuleCommandSink;

constexpr auto kRestartWhistle = Seconds(2);
constexpr TickSpan kHalfPreparation{10};
constexpr TickSpan kHalfWhistle{20};
constexpr TickSpan kPostRestartRelax{40};

// Initial engineering bounds, not calibrated match-duration distributions.
struct RestartPolicy {
  TickSpan minimum_delay;
  TickSpan maximum_delay;
};

RestartPolicy PolicyFor(e_GameMode mode) {
  switch (mode) {
    case e_GameMode_FreeKick: return {TickSpan{}, Seconds(30)};  // Quick restart allowed.
    case e_GameMode_ThrowIn: return {Seconds(2), Seconds(20)};
    case e_GameMode_GoalKick: return {Seconds(3), Seconds(30)};
    case e_GameMode_Corner: return {Seconds(5), Seconds(30)};
    case e_GameMode_Penalty: return {Seconds(2), Seconds(30)};
    case e_GameMode_KickOff: return {Seconds(5), Seconds(30)};
    default: throw std::logic_error("unsupported restart policy");
  }
}

std::vector<Player*> ActivePlayers(const RuntimeTickFacts& facts) {
  std::vector<Player*> active;
  for (Player* actor : facts.home.players) {
    if (actor->IsActive()) active.push_back(actor);
  }
  for (Player* actor : facts.away.players) {
    if (actor->IsActive()) active.push_back(actor);
  }
  return active;
}
}  // namespace

void RestartController::BeginMatch(int kickoff_team_id, const Vector3& kickoff_position) {
  context_ = RestartContext{};
  context_.active = true;
  context_.kind = e_GameMode_KickOff;
  context_.team_id = kickoff_team_id;
  context_.setpiece_team = kickoff_team_id;
  context_.stop_tick = {};
  context_.prepare_tick = {};
  context_.start_tick = Tick{} + kRestartWhistle;
  context_.position = kickoff_position;
  context_.taker = nullptr;
  context_.end_phase = true;
  post_restart_relax_ = {};
  next_restart_id_ = 1;
}

void RestartController::OnPeriodEnded(MatchPhase ended_phase, Tick now,
                                      const Vector3& kickoff_position, int kickoff_team_id) {
  context_.active = false;
  context_.taker = nullptr;
  context_.end_phase = false;
  context_.state.reset();
  if (ended_phase == MatchPhase::SecondHalf) return;

  // Half time: abandon the pending restart and prepare the next kickoff.
  context_.kind = e_GameMode_KickOff;
  context_.stop_tick = now;
  context_.prepare_tick = context_.stop_tick + kHalfPreparation;
  context_.start_tick = context_.prepare_tick + kHalfWhistle;
  context_.position = kickoff_position;
  context_.active = true;
  context_.end_phase = true;
  context_.team_id = kickoff_team_id;
  context_.setpiece_team = kickoff_team_id;
}

void RestartController::Clear() {
  context_ = RestartContext{};
  post_restart_relax_ = {};
}

std::vector<football::sim::event::MatchEvent> RestartController::Award(
    const football::sim::event::AwardRestartRuling& ruling, Tick now) {
  const RestartPolicy policy = PolicyFor(ruling.kind);
  RestartState state;
  state.entered_tick = now;
  state.earliest_restart_tick = state.entered_tick + policy.minimum_delay + ruling.administration;
  state.timeout_tick = state.entered_tick + policy.maximum_delay + ruling.administration;

  context_ = RestartContext{};
  context_.active = true;
  context_.kind = ruling.kind;
  context_.team_id = static_cast<int>(ruling.team);
  context_.setpiece_team = context_.team_id;
  context_.position = ruling.position;
  context_.taker = nullptr;
  context_.end_phase = false;
  context_.stop_tick = now;
  context_.id = next_restart_id_++;
  context_.state = std::move(state);

  return {football::sim::event::RestartAwardedEvent{
      now, ruling.team, ruling.kind, ruling.position, context_.id}};
}

std::optional<football::sim::event::RestartTakenEvent> RestartController::ConfirmRelease(
    Tick now) {
  if (!(context_.active && context_.state &&
        context_.state->phase == RestartPhase::Ready)) {
    return std::nullopt;
  }
  context_.state->phase = RestartPhase::Taken;
  football::sim::event::RestartTakenEvent taken;
  taken.tick = now;
  taken.team = static_cast<TeamSide>(context_.team_id);
  taken.kind = context_.kind;
  taken.taker = context_.taker ? context_.taker->GetID()
                                : football::player::kInvalidPlayerId;
  taken.restart_id = context_.id;
  return taken;
}

void RestartController::TickPostRestartGrace() {
  if (post_restart_relax_ > TickSpan{}) {
    post_restart_relax_ = post_restart_relax_ - TickSpan{1};
  }
}

RestartController::TickOutcome RestartController::Advance(
    const RuntimeTickFacts& facts, const MatchOptions& options, football::math::Rng& rng,
    RuleCommandSink& commands, const RestartPlanningTable& planning) {
  TickOutcome outcome;
  if (facts.phase == MatchPhase::Finished) return outcome;
  if (context_.active && context_.state) {
    ProcessRestart(facts, options, rng, commands, outcome, planning);
    TickPostRestartGrace();
    return outcome;
  }
  if (!(facts.play_authorized && !facts.set_piece_active)) {
    // not in play, maybe something needs to happen?
    if (!facts.play_authorized && !facts.set_piece_active && context_.active == true) {
      if (context_.taker == nullptr && facts.now >= context_.prepare_tick) {
        if (context_.end_phase == true) {
          if (facts.phase == MatchPhase::PreMatch) {
            commands.SetPhase(MatchPhase::FirstHalf);
          }
          context_.end_phase = false;
        }
        // Deterministic reseed before positioning players for every restart.
        rng.Seed(options.game_engine_random_seed);
        PrepareCeremonialKickOff(facts, options, rng, commands);
        outcome.ceremonial_setup = true;
        // The ceremony is a Runtime procedure: confirm the award directly.
        context_.id = next_restart_id_++;
        outcome.events.push_back(football::sim::event::RestartAwardedEvent{
            facts.now, static_cast<TeamSide>(context_.team_id), e_GameMode_KickOff,
            facts.stadium_to_home.Position(context_.position), context_.id});
      }
      if (context_.taker != nullptr && facts.now >= context_.start_tick) {
        // blow whistle and wait for set piece taker to touch the ball
        commands.StartPlay();
        commands.StartSetPiece();
        // Keep ceremonial schedule/placement, but never fabricate a kickoff.
        // Authorization and accepted contact share the ordinary release states.
        RestartState ready;
        ready.entered_tick = context_.stop_tick;
        ready.earliest_restart_tick = context_.start_tick;
        ready.timeout_tick = context_.start_tick;
        ready.phase = RestartPhase::Ready;
        ready.setup_done = true;
        context_.state = std::move(ready);
      }
    }
  }
  TickPostRestartGrace();
  return outcome;
}

void RestartController::PrepareCeremonialKickOff(const RuntimeTickFacts& facts,
                                                 const MatchOptions& options,
                                                 football::math::Rng& rng,
                                                 RuleCommandSink& commands) {
  // Opening/half-time placement is intentionally separate from ordinary readiness.
  commands.ResetSituation(options.reverse_team_processing ? -context_.position : context_.position);
  const TeamView& first = facts.first_team == 0 ? facts.home : facts.away;
  const TeamView& second = facts.first_team == 0 ? facts.away : facts.home;
  Player* first_taker = PositionRestartPlayers(facts.pitch, first, e_GameMode_KickOff, second,
      context_.setpiece_team, context_.team_id, facts.ball, facts.regulation, options, rng, commands);
  Player* second_taker = PositionRestartPlayers(facts.pitch, second, e_GameMode_KickOff, first,
      context_.setpiece_team, context_.team_id, facts.ball, facts.regulation, options, rng, commands);
  context_.taker = context_.team_id == facts.first_team ? first_taker : second_taker;
}

void RestartController::ProcessRestart(const RuntimeTickFacts& facts, const MatchOptions& options,
                                       football::math::Rng& rng, RuleCommandSink& commands,
                                       TickOutcome& outcome,
                                       const RestartPlanningTable& planning) {
  auto& state = *context_.state;
  // Live actor index for this execution instant; plans store ids only.
  const RestartRoster roster{facts.home.players, facts.away.players};
  if (state.phase == RestartPhase::Taken) {
    state.phase = RestartPhase::InPlay;
    context_.active = false;
    commands.StopSetPiece();
    post_restart_relax_ = kPostRestartRelax;
    outcome.restart_taken = true;
    return;
  }
  if (state.phase != RestartPhase::Pending) return;
  if (!state.setup_done) {
    rng.Seed(options.game_engine_random_seed);
    if (context_.kind == e_GameMode_FreeKick) {
      context_.position.coords[0] = clamp(context_.position.coords[0], -0.95f * facts.pitch.half_length(), 0.95f * facts.pitch.half_length());
      context_.position.coords[1] = clamp(context_.position.coords[1], -0.95f * facts.pitch.half_width(), 0.95f * facts.pitch.half_width());
    }
    commands.ResetSituation(facts.ball_to_home.Position(context_.position));
    state.plan = PlanRestart(facts.pitch, facts.ball_to_home.Position(
        facts.ball.state().position), ActivePlayers(facts), context_.kind,
        context_.setpiece_team, planning);
    for (const auto& target : state.plan.players) {
      Player* actor = roster.Find(target.player);
      if (actor == nullptr) continue;
      const auto frame = FromHomePitchFrame(actor->GetTeamSide() == football::sim::TeamSide::Home, actor->GetDynamicSide());
      actor->ResetPosition(actor->GetPosition(), frame.Position(state.plan.ball_position));
    }
    context_.taker = roster.Find(state.plan.taker);
    state.setup_done = true;
  }
  if (!context_.taker || !context_.taker->IsActive()) {
    state.plan = PlanRestart(facts.pitch, facts.ball_to_home.Position(
        facts.ball.state().position), ActivePlayers(facts), context_.kind,
        context_.setpiece_team, planning);
    context_.taker = roster.Find(state.plan.taker);
  }
  if (facts.now < state.earliest_restart_tick) return;
  if (!state.used_timeout_placement && facts.now >= state.timeout_tick) {
    PlaceRestartPlayersAtTimeout(state.plan, roster);
    commands.ResetBall(facts.ball_to_home.Position(state.plan.ball_position));
    state.used_timeout_placement = true;
  }
  const auto ball_position = facts.ball_to_home.Position(facts.ball.state().position);
  if (!RestartPlayersReady(state.plan, facts.pitch, roster) ||
      std::fabs(ball_position.coords[2] - 0.11f) > 0.03f ||
      (ball_position.Get2D() - state.plan.ball_position).GetLength() > 0.05f ||
      facts.ball.state().velocity.GetLength() > 0.5f) return;
  state.phase = RestartPhase::Ready;
  context_.start_tick = facts.now;  // Fact: authorization instant, not a preset delay.
  if (context_.kind == e_GameMode_ThrowIn) {
    context_.taker->SelectRetainAnim();
    commands.SetBallRetainer(context_.taker);
  }
  commands.StartPlay();
  commands.StartSetPiece();
}

}  // namespace football::sim
