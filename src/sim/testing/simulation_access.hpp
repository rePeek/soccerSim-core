#ifndef FOOTBALL_SIM_TESTING_SIMULATION_ACCESS_HPP
#define FOOTBALL_SIM_TESTING_SIMULATION_ACCESS_HPP

#include <cstdint>
#include <stdexcept>
#include <vector>

#include "sim/simulation.hpp"
#include "sim/animation/library.hpp"
#include "football/ball/ball.hpp"
#include "sim/observation/pitch_frame.hpp"
#include "sim/player/player.hpp"
#include "sim/player/player_control_builder.hpp"
#include "sim/rules/referee.hpp"
#include "sim/team/team.hpp"

namespace football::sim::testing {

// Internal diagnostics only. Not exported by sim_contracts or used by actors.
// Product execution remains Init/Step/Observe/Finished/Result/Stop.
class SimulationAccess {
 public:
  static PlayerTickContext PlayerTickOf(Simulation& simulation, Player& actor) {
    return simulation.PlayerTickFacts(actor);
  }
  static MatchClock& ClockOf(Simulation& simulation) {
    if (!simulation.clock_) throw std::logic_error("simulation has no match");
    return *simulation.clock_;
  }
  static void SendOff(Simulation& simulation, Player& actor) {
    const auto tick = PlayerTickOf(simulation, actor);
    actor.SendOff(tick.ball, tick.now, tick.rng);
  }
  static void Deactivate(Simulation& simulation, Player& actor) {
    const auto tick = PlayerTickOf(simulation, actor);
    actor.Deactivate(tick.ball, tick.now);
  }
  static PlayerCommandInputs CommandInputsOf(Simulation& simulation) {
    if (!simulation.ball_) throw std::logic_error("simulation has no match");
    return {*simulation.ball_, simulation.touches_, RulesOf(simulation).GetBuffer(),
            simulation.ball_retainer_, simulation.pitch_};
  }
  static BallTouchSink& EventsOf(Simulation& simulation) {
    if (!simulation.touch_sink_) throw std::logic_error("simulation has no match");
    return *simulation.touch_sink_;
  }
  static event::TouchState& TouchesOf(Simulation& simulation) {
    if (!simulation.ball_) throw std::logic_error("simulation has no match");
    return simulation.touches_;
  }
  static Referee& RulesOf(Simulation& simulation) {
    if (!simulation.referee_) throw std::logic_error("simulation has no match");
    return *simulation.referee_;
  }
  static rules::RuleCommandSink& CommandsOf(Simulation& simulation) {
    if (!simulation.rule_commands_) throw std::logic_error("simulation has no match");
    return *simulation.rule_commands_;
  }
  static PlayerRuntimeSink& RuntimeOf(Simulation& simulation) {
    if (!simulation.player_runtime_sink_) throw std::logic_error("simulation has no match");
    return *simulation.player_runtime_sink_;
  }
  static rules::RefereeTickFacts RefereeFactsOf(const Simulation& simulation) {
    return simulation.RefereeFacts();
  }
  static void ProcessRules(Simulation& simulation, Referee& referee) {
    referee.Process(RefereeFactsOf(simulation), simulation.options(),
                    simulation.rng_, CommandsOf(simulation));
  }

  // Runtime facts; the transitional Match facade is gone, so diagnostics name
  // the exact state they read.
  static football::ball::Ball* BallOf(Simulation& s) {
    if (!s.ball_) throw std::logic_error("simulation has no match");
    return s.ball_.get();
  }
  static const football::ball::Ball* BallOf(const Simulation& s) { return s.ball_.get(); }
  static Team* TeamOf(Simulation& s, int team_id) { return s.teams_[team_id].get(); }
  static const Team* TeamOf(const Simulation& s, int team_id) {
    return s.teams_[team_id].get();
  }
  static blunted::Rng& RngOf(Simulation& s) { return s.rng_; }
  static football::sim::Tick NowOf(Simulation& s) { return s.clock_->now(); }
  static football::sim::TickSpan RegulationTimeOf(Simulation& s) {
    return s.clock_->RegulationTime();
  }
  static football::sim::TickSpan BallInPlayTimeOf(Simulation& s) {
    return s.clock_->BallInPlayTime();
  }
  static bool IsInitializedOf(const Simulation& s) { return s.ball_ != nullptr; }
  static bool IsInPlayOf(Simulation& s) { return s.play_authorized_; }
  static bool IsInSetPieceOf(Simulation& s) { return s.set_piece_active_; }
  static bool IsBallInPlayOf(Simulation& s) { return s.clock_->IsBallInPlay(); }
  static bool IsHalfUnderwayOf(Simulation& s) { return s.clock_->IsHalfUnderway(); }
  static football::ball::BallEnvironment BallEnvironmentOf(Simulation& s) {
    return {s.ball_in_goal_};
  }
  static int ScoreOf(Simulation& s, int team_id) { return s.score_[team_id]; }
  static float PossessionFactorOf(Simulation& s) { return s.possession_60_seconds_ / 60.0f; }
  static MatchPhase PhaseOf(Simulation& s) { return s.phase_; }
  static const AnimationLibrary& AnimationLibraryOf(Simulation& s) { return *s.animations_; }
  static void ActiveTeamPlayersOf(Simulation& s, int team_id, std::vector<Player*>& out) {
    s.teams_[team_id]->GetActivePlayers(out);
  }
  static int FirstTeamOf(Simulation& s) { return s.first_team_; }
  static int SecondTeamOf(Simulation& s) { return s.second_team_; }
  static bool IsBallMirroredOf(Simulation& s) { return s.ball_mirrored_; }
  static Player* DesignatedPlayerOf(Simulation& s) { return s.designated_possession_player_; }
  static Player* BallRetainerOf(Simulation& s) { return s.ball_retainer_; }
  static const football::model::Pitch& PitchOf(Simulation& s) { return s.pitch_; }
  static const football::model::Pitch& PitchOf(const Simulation& s) { return s.pitch_; }
  static const MatchOptions& OptionsOf(Simulation& s) { return s.options_; }
  static bool MayTouchBallOf(Simulation& s, const Player& actor) {
    return s.clock_->IsBallInPlay() ||
        (s.play_authorized_ && s.set_piece_active_ && s.referee_->GetBuffer().active &&
         s.referee_->GetBuffer().taker == &actor);
  }
  static bool IsGoalScoredOf(Simulation& s) { return s.goal_scored_; }
  static bool IsBallInGoalOf(Simulation& s) { return s.ball_in_goal_; }
  static Team* LastGoalTeamOf(Simulation& s) { return s.last_goal_team_; }
  static Referee* RefereeOf(Simulation& s) { return s.referee_.get(); }
  static Team* LastTouchTeamOf(Simulation& s) { return s.GetLastTouchTeam(); }
  static Player* LastGoalScorerOf(Simulation& s) { return s.last_goal_scorer_; }
  static int LastTouchTeamIDOf(Simulation& s) { return s.touches_.last_team; }
  static int LastTouchTeamIDOf(Simulation& s, e_TouchType type) {
    return s.touches_.last_team_by_type[type];
  }
  static Player* LastTouchPlayerOf(Simulation& s) { return s.GetLastTouchPlayer(); }
  static std::uint64_t ResetSequenceOf(Simulation& s) { return s.reset_sequence_; }

  // Test/diagnostic lifecycle escape hatches; product code uses Step().
  static void SetPhase(Simulation& s, MatchPhase phase) { s.SetMatchPhase(phase); }
  static void StartPlay(Simulation& s) { s.StartPlay(); }
  static void StopPlay(Simulation& s) { s.StopPlay(); }
  static void StartSetPiece(Simulation& s) { s.StartSetPiece(); }
  static void StopSetPiece(Simulation& s) { s.StopSetPiece(); }
  static void StartBallInPlay(Simulation& s) { s.StartBallInPlay(); }
  static void EndHalf(Simulation& s) { s.EndHalf(); }
  static void SetGoalScored(Simulation& s, bool value) { s.SetGoalScored(value); }
  static void SetBallRetainer(Simulation& s, Player* retainer) { s.SetBallRetainer(retainer); }
  static void RequestChangeOfEnds(Simulation& s) { s.pending_change_of_ends_ = true; }
  static void ResetBall(Simulation& s, const blunted::Vector3& position) {
    s.ball_->ResetSituation(position);
  }
};

} // namespace football::sim::testing
#endif
