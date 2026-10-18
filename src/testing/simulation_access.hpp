#ifndef FOOTBALL_SIM_TESTING_SIMULATION_ACCESS_HPP
#define FOOTBALL_SIM_TESTING_SIMULATION_ACCESS_HPP

#include <cstdint>
#include "football/pitch/pitch.hpp"
#include <stdexcept>
#include <vector>

#include "runtime/simulation.hpp"
#include "football/animation/library.hpp"
#include "football/ball/ball.hpp"
#include "observation/pitch_frame.hpp"
#include "player/player.hpp"
#include "player/player_control_builder.hpp"
#include "player/player_team_view.hpp"
#include "referee/referee.hpp"

namespace football::sim::testing {

// Internal diagnostics only. Not exported by sim_contracts or used by actors.
// Product execution remains Init/Step/Observe/Finished/Result/Stop.
class SimulationAccess {
 public:
  static const football::team::TeamDefinition& TeamOf(const Simulation& simulation,
                                                      TeamSide side) {
    return simulation.TeamOf(static_cast<int>(side));
  }
  static PlayerTickContext PlayerTickOf(Simulation& simulation, Player& actor) {
    return simulation.PlayerTickFacts(actor);
  }
  static MatchClock& ClockOf(Simulation& simulation) {
    if (!simulation.clock_) throw std::logic_error("simulation has no match");
    return *simulation.clock_;
  }
  static void SendOff(Simulation& simulation, Player& actor) {
    const auto tick = PlayerTickOf(simulation, actor);
    actor.SendOff(tick.ball, tick.now, tick.rng, simulation.send_off_sink_.get());
  }
  static void Deactivate(Simulation& simulation, Player& actor) {
    const auto tick = PlayerTickOf(simulation, actor);
    actor.Deactivate(tick.ball, tick.now);
  }
  // The caller supplies the borrowed team view: PlayerCommandInputs stores a
  // reference, so the helper cannot manufacture a temporary one.
  static PlayerCommandInputs CommandInputsOf(Simulation& simulation,
                                             const PlayerTeamView& own_team) {
    if (!simulation.ball_) throw std::logic_error("simulation has no match");
    return {*simulation.ball_, simulation.touches_, simulation.restart_context(),
            simulation.ball_retainer_, simulation.pitch_, own_team};
  }
  static AcceptedTouchSink& EventsOf(Simulation& simulation) {
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
  static void ReplaceRules(Simulation& s, std::unique_ptr<Referee> rules) {
    s.referee_ = std::move(rules);
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
  static event::RulingSink& RulingsOf(Simulation& s) { return *s.ruling_sink_; }
  static void CommitRulings(Simulation& s) { s.CommitPendingRulings(); }
  static void ProcessReferee(Simulation& s) { s.ProcessReferee(); }
  static const auto& MatchEventsOf(const Simulation& s) { return s.event_log_.events(); }
  static std::size_t PendingRulingsOf(const Simulation& s) { return s.pending_rulings_.size(); }
  // Test-only orchestration of one referee instant. Runtime owns restart
  // execution, so this is the same synchronous boundary product code uses.
  static void ProcessRules(Simulation& simulation, Referee& referee) {
    (void)referee;
    simulation.ProcessReferee();
  }

  static const football::sim::RestartContext& RestartContextOf(const Simulation& s) {
    return s.restarts_.context();
  }
  static void ClearRestart(Simulation& s) { s.restarts_.Clear(); }
  static football::sim::PlayState PlayStateOf(const Simulation& s) {
    return s.CurrentPlayState();
  }

  // Runtime facts; the transitional Match facade is gone, so diagnostics name
  // the exact state they read.
  static football::ball::Ball* BallOf(Simulation& s) {
    if (!s.ball_) throw std::logic_error("simulation has no match");
    return s.ball_.get();
  }
  static const football::ball::Ball* BallOf(const Simulation& s) { return s.ball_.get(); }
  static void EnablePreparedTickProduction(Simulation& s, bool enabled) { s.EnablePreparedTickProduction(enabled); }
  static const auto& CommittedBallTickOf(const Simulation& s) { return s.committed_ball_tick_; }
  static const auto& RuleTouchesOf(const Simulation& s) { return s.rule_touches_; }
  // Borrowed view of one team, as rules/observation see it.
  static football::sim::TeamView TeamViewOf(Simulation& s, int team_id) {
    return s.TeamViewOf(team_id);
  }
  static football::math::Rng& RngOf(Simulation& s) { return s.rng_; }
  // Runtime-owned orientation behaviour, reachable from the harness without a
  // Team object: MirrorTeam flips the execution frame, SwitchTeamEnds is the
  // half-time change of ends.
  static void MirrorTeam(Simulation& s, int team_id) { s.MirrorTeam(team_id); }
  static void SwitchTeamEnds(Simulation& s, int team_id) {
    s.SwitchTeamEnds(team_id);
  }
  static PlayerTeamFacts TeamFactsOf(const Simulation& s, int team_id) {
    return s.TeamFactsFor(team_id);
  }
  // The runtime-owned possession inputs: the roster and the match state a team
  // owns. Exposed so the harness can exercise possession without a Team object.
  static football::sim::Roster& RosterOf(Simulation& s, int team_id) {
    return s.RosterOf(team_id);
  }
  static football::sim::TeamFrame& TeamFrameOf(Simulation& s, int team_id) {
    return s.team_frames_[team_id];
  }
  static football::sim::TeamMatchState& TeamStateOf(Simulation& s, int team_id) {
    return s.team_states_[team_id];
  }
  static football::sim::ActiveLineupState& ActiveLineupOf(Simulation& s, int team_id) {
    return s.active_lineups_[team_id];
  }
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
  static int ScoreOf(Simulation& s, int team_id) { return s.score_[team_id]; }
  static float PossessionFactorOf(Simulation& s) { return s.possession_60_seconds_ / 60.0f; }
  static MatchPhase PhaseOf(Simulation& s) { return s.phase_; }
  static const AnimationLibrary& AnimationLibraryOf(Simulation& s) { return *s.animations_; }
  static void ActiveTeamPlayersOf(Simulation& s, int team_id, std::vector<Player*>& out) {
    s.GetActiveTeamPlayers(team_id, out);
  }
  static int FirstTeamOf(Simulation& s) { return s.first_team_; }
  static int SecondTeamOf(Simulation& s) { return s.second_team_; }
  static bool IsBallMirroredOf(Simulation& s) { return s.ball_mirrored_; }
  static Player* DesignatedPlayerOf(Simulation& s) { return s.designated_possession_player_; }
  static Player* BallRetainerOf(Simulation& s) { return s.ball_retainer_; }
  static const football::pitch::Pitch& PitchOf(Simulation& s) { return s.pitch_; }
  static const football::pitch::Pitch& PitchOf(const Simulation& s) { return s.pitch_; }
  static const MatchOptions& OptionsOf(Simulation& s) { return s.options_; }
  static bool MayTouchBallOf(Simulation& s, const Player& actor) {
    return s.clock_->IsBallInPlay() ||
        (s.play_authorized_ && s.set_piece_active_ && s.restarts_.context().active &&
         s.restarts_.context().taker == &actor);
  }
  static bool IsGoalScoredOf(Simulation& s) { return s.goal_scored_; }
  static bool IsBallInGoalOf(Simulation& s) { return s.ball_in_goal_; }
  static int LastGoalTeamOf(Simulation& s) { return s.last_goal_team_; }
  static Referee* RefereeOf(Simulation& s) { return s.referee_.get(); }
  static int LastTouchTeamOf(Simulation& s) { return s.GetLastTouchTeam(); }
  static Player* LastGoalScorerOf(Simulation& s) { return s.last_goal_scorer_; }
  static int LastTouchTeamIDOf(Simulation& s) { return s.touches_.last_team; }
  static int LastTouchTeamIDOf(Simulation& s, e_TouchType type) {
    return s.touches_.last_team_by_type[type];
  }
  static Player* LastTouchPlayerOf(Simulation& s) { return s.GetLastTouchPlayer(); }
  static std::uint64_t ResetSequenceOf(Simulation& s) { return s.reset_sequence_; }
  static const std::vector<FoulAssessment>& FoulAssessmentsOf(const Simulation& s) {
    return s.foul_assessments_;
  }
  // Accepted-touch log captured where AcceptedTouch was produced; shadow only.
  static const std::vector<football::sim::event::RecordedTouch>& RecordedTouchesOf(
      const Simulation& s) {
    return s.recorded_touches_;
  }
  static const PlayerBodyCollisionShadowReport& BodyCollisionShadowReportOf(
      const Simulation& s) {
    return s.body_shadow_report_;
  }
  static void EnableBodyCollisionShadow(Simulation& s, bool enabled) {
    s.body_shadow_enabled_ = enabled;
    s.DiscardBodyCollisionShadow();
    s.body_geometry_episodes_.Break();
    s.body_impact_episodes_.Break();
  }
  static void BeginBodyCollisionShadow(Simulation& s) { s.BeginBodyCollisionShadow(); }
  static const auto& BodyShadowPredictionsOf(const Simulation& s) {
    return s.body_shadow_predictions_;
  }
  static const auto& BodyColliderOwnersOf(const Simulation& s) {
    return s.body_collider_owners_;
  }
  static void EnableBodyPhysicsShadow(Simulation& s, bool enabled) {
    if (!s.ball_) throw std::logic_error("simulation has no match");
    s.DiscardBodyCollisionShadow();
    if (enabled) {
      s.body_shadow_enabled_ = true;
      s.body_physics_shadow_colliders_.reserve(s.snapshot_players_.size() * kPlayerBodyPartCount);
      const auto slots = s.snapshot_players_.size() * kPlayerBodyPartCount;
      s.body_physics_shadow_candidates_.reserve(slots);
      s.body_geometry_episodes_.Resize(slots);
      s.body_impact_episodes_.Resize(slots);
      s.body_physics_shadow_ball_ = std::make_unique<football::ball::Ball>(s.ball_config_, s.pitch_);
    } else {
      s.body_physics_shadow_ball_.reset();
    }
  }
  static const auto& BodyPhysicsShadowReportOf(const Simulation& s) {
    return s.body_physics_shadow_report_;
  }
  static const auto& BodyPhysicsShadowLatestOf(const Simulation& s) {
    return s.body_physics_shadow_latest_;
  }
  static const auto& BodyPhysicsShadowPendingOf(const Simulation& s) {
    return s.body_physics_shadow_tick_;
  }
  static const auto& BodyPhysicsShadowCandidatesOf(const Simulation& s) {
    return s.body_physics_shadow_candidates_;
  }
  static void EnableActiveTouchShadow(Simulation& s, bool enabled) { s.EnableActiveTouchShadow(enabled); }
  static void EnableActiveImpulseProduction(Simulation& s, bool enabled) { s.EnableActiveImpulseProduction(enabled); }
  static void EnableBodyPhysicsProduction(Simulation& s, bool enabled) { s.EnableBodyPhysicsProduction(enabled); }
  static void EnableBakedBodyProduction(Simulation& s, bool enabled) { s.EnableBakedBodyProduction(enabled); }
  static void EnableLocomotionAnimationAdaptation(Simulation& s, bool enabled) { s.EnableLocomotionAnimationAdaptation(enabled); }
  static void EnableMovementPoseBlend(Simulation& s, bool enabled) { s.EnableMovementPoseBlend(enabled); }
  static void InitializeProductionPhysics(Simulation& s) { s.InitializeProductionPhysics(); }
  static void ReleaseProductionPhysics(Simulation& s) { s.ReleaseProductionPhysics(); }
  static bool PreparedTickProductionOf(const Simulation& s) { return s.prepared_tick_production_; }
  static bool BodyPhysicsProductionOf(const Simulation& s) { return s.body_physics_production_; }
  static bool BakedBodyProductionOf(const Simulation& s) { return s.baked_body_production_; }
  static const auto& PreparationBallOf(const Simulation& s) { return s.preparation_ball_; }
  static const auto& RuleContactEpisodesOf(const Simulation& s) { return s.rule_contact_episodes_; }
  static const auto& BakedBodyTapeOf(const Simulation& s) { return s.baked_body_colliders_; }
  static const auto& BakedBodyStartsOf(const Simulation& s) { return s.baked_body_starts_; }
  static const auto& ActiveTouchShadowReportOf(const Simulation& s) { return s.active_touch_shadow_report_; }
  static const auto& ActiveArbitrationOf(const Simulation& s) { return s.active_arbitration_; }
  static const auto& PendingActiveImpulseOf(const Simulation& s) { return s.pending_active_impulse_; }

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
  static void ResetBall(Simulation& s, const football::math::Vector3& position) {
    s.PlaceBallAt(position);
  }
  // Ground-resting placement for a standalone Ball under test. The removed
  // Ball::ResetSituation is expressed here through the stable Ball::Reset API;
  // product restart placement lives in Simulation::PlaceBallAt.
  static void ResetBallTo(football::ball::Ball& ball,
                          const football::math::Vector3& position) {
    ball.Reset(football::ball::BallState{
        position + football::math::Vector3(0, 0, ball.config().radius()),
        football::math::Vector3(0), football::math::Vector3(0), football::math::Quaternion{}});
  }
};

} // namespace football::sim::testing
#endif
