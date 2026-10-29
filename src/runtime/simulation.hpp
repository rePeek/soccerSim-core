#ifndef FOOTBALL_SIM_SIMULATION_HPP
#define FOOTBALL_SIM_SIMULATION_HPP

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <map>
#include <vector>

#include "football/player/player_control_set.hpp"
#include "football/runtime/match_config.hpp"
#include "football/team/team.hpp"
#include "football/runtime/active_lineup.hpp"
#include "runtime/team_setup_adapter.hpp"
#include "football/pitch/pitch.hpp"
#include "football/ball/ball_model.hpp"
#include "football/foundation/collision/sweep.hpp"
#include "football/runtime/simulation_config.hpp"
#include "football/observation/world_state.hpp"
#include "observation/mentalimage.hpp"
#include "observation/snapshot_history.hpp"
#include "observation/player_slots.hpp"
#include "football/runtime/result.hpp"
#include "runtime/team_facts.hpp"
#include "runtime/clock.hpp"
#include "runtime/restart_controller.hpp"
#include "runtime/restart_context.hpp"
#include "runtime/player_authorization_port.hpp"
#include "runtime/play_state.hpp"
#include "event/accepted_touch_sink.hpp"
#include "event/event_recognizer.hpp"
#include "event/touch_state.hpp"
#include "event/touch_record.hpp"
#include "event/rule_touch.hpp"

#include "referee/referee_tick_facts.hpp"
#include "runtime/rule_command_sink.hpp"
#include "player/execution/player_tick_context.hpp"
#include "player/execution/player_runtime_sink.hpp"
#include "player/execution/player_send_off_sink.hpp"
#include "football/contact/foul_assessment.hpp"
#include "player/execution/diagnostics/player_body_collision_shadow.hpp"
#include "player/execution/diagnostics/player_active_touch_shadow.hpp"
#include "player/execution/player_baked_pose_sample.hpp"

#include "referee/ruling.hpp"
#include "football/event/match_event.hpp"
#include "event/event_log.hpp"
#include "referee/referee_view.hpp"

class Player;
namespace football::ball { class Ball; }
class Referee;
class AnimationLibrary;
namespace football::sim::testing { class SimulationAccess; }

// Simulation owns the whole match composition: clock/phase/score, ball, two
// Team entities (each owning its Players; Runtime owns their team state),
// referee, touch/possession state, history and config. Step() is the only
// orchestration point.
class Simulation {
 public:
  Simulation();
  ~Simulation();

  void Init(const football::sim::TeamSetup& home,
            const football::sim::TeamSetup& away,
            const football::pitch::Pitch& pitch, MatchOptions options,
            const football::ball::BallModel& ball_config = football::ball::BallModel{});
  bool Stop();
  void Step(const PlayerControlSet& controls);
  bool IsInPlay() const;
  WorldState Observe() const;
  bool Finished() const;
  // Final only; throws std::logic_error before full time or without a match.
  MatchResult Result() const;

  // Single-threaded, read-only history. No disk access or runtime mutation.
  const football::sim::observation::SnapshotHistory& Snapshots() const { return snapshot_history_; }
  const football::sim::observation::SnapshotMetadata& SnapshotMetadata() const { return snapshot_metadata_; }
  // Static player -> TeamSide association, valid for the whole match.
  const football::sim::observation::PlayerSlotTable& PlayerSlots() const { return player_slots_; }
  // Realized skeletal pose at physical commitment, in the common actor frame.
  // Same FK source as the six CCD primitives; slots match SnapshotMetadata.
  // Empty after placement/reset until the next realization. Borrow expires on Step.
  std::span<const std::optional<PlayerBakedPoseSample>> CommittedBakedPoses() const {
    if (!baked_body_production_ || baked_pose_generation_ != reset_sequence_) return {};
    return baked_body_poses_;
  }
  // Whole-step summaries published with the latest Snapshot, not contact facts.
  const std::vector<football::sim::event::EventTrajectory>& EventTrajectories() const {
    return recognizer_.trajectories();
  }
  // Incremental, owning read of confirmed MatchEvents after `cursor`. Read-only
  // with respect to rules/physics/RNG; a cursor from another match is rebased.
  std::vector<football::sim::event::MatchEventRecord> ReadEvents(
      football::sim::event::MatchEventCursor& cursor) const {
    return event_log_.EventsSince(cursor);
  }

  // TODO: test/diagnostic escape hatch; not a general integration API.
  // Transitional test/diagnostic sampling; pointers expire on capture/reset/Stop.
  MentalImage* GetMentalImage(football::sim::TickSpan history);
  MentalImage* GetMentalImage(std::chrono::milliseconds history);
  // Transitional test/diagnostic clock-only advance: no actors, rules or Step count.
  void AdvanceTime(football::sim::TickSpan delta);
  // Runtime composition, also transitional test/diagnostic escapes.
  void ResetSituation(const football::math::Vector3& focus_position);
  // Transitional diagnostic impulse; synchronous physics/history/possession only.
  void TouchBall(const football::math::Vector3& impulse);

 private:
  friend class football::sim::testing::SimulationAccess;
  class RuleCommands;
  class TouchEvents;
  class RulingEvents;
  class ActiveTouchEvents;
  // Applies the team-level follow-up of a send-off; Player only reports it.
  class SendOffEvents;

  // Synchronous accepted-touch dispatch: bookkeeping, recognition and the
  // referee consume happen at the exact legacy boundary and in legacy order.
  Player* FindPlayerById(football::player::PlayerId id) const;
  void DispatchTouch(const football::sim::event::AcceptedTouch& touch);
  // Law 12 evidence for one frozen foul: Runtime samples the live geometry
  // (goal, victim, ball, covering opponents) and the referee classifies the
  // sanction from it. It never mutates an actor.
  bool DeniedGoalOpportunity(const football::sim::FoulAssessment& fact) const;
  void CommitPendingRulings();
  void CommitRuling(const football::sim::event::RefereeRuling& ruling);
  void ValidateRuling(const football::sim::event::RefereeRuling& ruling) const;
  // Rejects mutually conflicting rulings in one logical batch before any of
  // them is committed.
  void ValidateRulingBatch() const;
  bool CardAlreadyCommitted(const football::sim::event::CardRuling& ruling) const;
  // Exhaustive ruling execution: every defined verdict has an explicit handler,
  // so a new ruling type cannot be silently dropped by the executor.
  void ApplyGoalRuling(const football::sim::event::AwardGoalRuling& ruling);
  void ApplyRestartRuling(const football::sim::event::AwardRestartRuling& ruling);
  void ApplyBallInPlayRuling(const football::sim::event::BallInPlayRuling& ruling);
  void ApplyCardRuling(const football::sim::event::CardRuling& ruling);
  // Player/formation/possession follow-up when a player leaves the pitch.
  void HandlePlayerSentOff(Player& player);
  // One live team aggregate per side. It owns the static definition and the
  // actual Player objects; Runtime owns only the match execution state around
  // it (active_lineups_/team_orientations_/team_states_).
  football::team::Team& TeamOf(int id);
  const football::team::Team& TeamOf(int id) const;
  football::sim::ActiveLineupState& ActiveLineupOf(int id) {
    return active_lineups_[id];
  }
  // Shared by Stop and every initialization failure; handles partial owners.
  void ClearComposition();
  void InitTeamPlayers(int team_id, football::team::Team& team,
                       bool explicit_lineup, std::uint8_t first_schedule_phase);
  // runtime-owned Team/frame/state; no allocation, nothing retained.
  football::sim::TeamView TeamViewOf(int team_id) const;
  // Team orientation is runtime-owned behaviour: only the permanent change of
  // ends mutates it. Nothing else writes a TeamOrientation; Pitch does not know
  // the half, Referee does not change a direction, Team owns no live
  // orientation and AI never schedules a change of ends.
  void SwitchTeamEnds(int team_id);
  PlayerTeamFacts TeamFactsFor(int team_id) const;
  void PublishTeamFacts(int team_id);
  // Resets one team's match state and its actors in the legacy order: state
  // first, then the designated player, then per-player ResetSituation and fact
  // publication inside the same loop (not batched afterwards).
  void ResetTeamSituation(int team_id,
                          const football::math::Vector3& focus_position,
                          football::sim::Tick now);
  void RelaxTeamFatigue(int team_id, float amount);
  void ProcessReferee();
  // Recognized behavior transitions -> confirmed MatchEvents.
  void CommitEventTransitions(football::sim::Tick now);
  // Runtime-owned restart execution for one instant; reports rule-state
  // side effects the Referee must apply.
  void AdvanceRestart(const football::sim::RuntimeTickFacts& facts);
  football::sim::RuntimeTickFacts RefereeFacts() const;
  // The Referee's own value facts, derived from RefereeFacts().
  football::sim::rules::RefereeTickFacts RefereeValueFacts() const;
  football::sim::PlayerTickContext PlayerTickFacts(const Player& actor);
  void EnsureAnimationLibrary();
  void CaptureMentalImage(const football::ball::Ball* view = nullptr);
  void StepImpl(const PlayerControlSet& controls);
  // A stopped instant with no restart to organize and outside the opening/
  // half-time placement window executes no player/ball simulation: only the
  // clock advances. This is PlayState::Stopped outside the ceremony window.
  bool ShouldSkipPlayerSimulation() const;
  void CaptureSnapshot();
  void EndPeriod();
  // The single permanent end change of the match. EndPeriod only publishes the
  // whistle and sets pending_change_of_ends_; StepImpl applies this transaction
  // once, at the entry of the next tick, before that tick is counted.
  void ApplyChangeOfEnds();
  void UpdateRecentPossession(football::sim::TickSpan admitted);
  void BeginBodyCollisionShadow();
  void MeasureBodyShadowEndpoint(const Player& player);
  void CompleteBodyCollisionShadow();
  void DiscardBodyCollisionShadow();
  void BeginBodyPhysicsShadow();
  void CompleteBodyPhysicsShadow();
  void EnableActiveTouchShadow(bool enabled);
  // P5e: the real candidate observer exists whenever candidate capture is on;
  // the diagnostic report is only an optional extra duty of that observer.
  void EnsureActiveTouchObserver();
  // P5e: this is the real per-tick arbitration input, independent of any shadow.
  // Candidates arrive from actors; the winner becomes pending_active_impulse_.
  void SubmitActiveTouchCandidate(const ActiveTouchObservation& observation);
  void ArbitratePendingActiveTouches();
  // P5c: opt-in production takeover of the contact-point spin model for the
  // migrated actions. Diagnostic/test entry point; product code never enables it.
  void EnableActiveImpulseProduction(bool enabled);
  // P4d-2: opt-in authority switch for passive body collisions. When enabled the
  // production Ball integrates the predicted body colliders and the legacy
  // resolver is skipped, so exactly one authority changes the ball.
  void EnableBodyPhysicsProduction(bool enabled);
  void EnablePreparedTickProduction(bool enabled);
  void RunPreparedPlayerTick();
  void EnableBakedBodyProduction(bool enabled);
  // LOCO-2.3b: toggles the motion-driven Movement animation reorder on every
  // active player. Default ON in production; OFF is the controlled A/B leg that
  // proves the reorder never changes the committed trajectory.
  void EnableLocomotionAnimationAdaptation(bool enabled);
  void EnableMovementPoseBlend(bool enabled);
  // P7a: single production-entry/teardown points. Init/Stop own the lifecycle;
  // these are the only places the three authority switches are flipped.
  void InitializeProductionPhysics();
  void ReleaseProductionPhysics();
  void BeginBakedBodyTick();
  void CompleteBakedBodyPreparation();
  void BuildBakedBodyTape(std::span<const std::optional<PlayerBakedPoseSample>> endpoints);
  void PublishRuleTouch(football::sim::event::RuleTouch touch);
  void PublishBodyRuleTouches(const football::ball::BallStepResult& result,
      std::span<const football::geometry::ColliderMotion> bodies);

  // Lifecycle mutations, published through the write-only rule/player ports.
  // Match restart semantics: place the ball at a kickoff/free-kick focus with
  // zero motion. Ball itself only exposes Reset(BallState).
  void PlaceBallAt(const football::math::Vector3& focus);
  void SetMatchPhase(MatchPhase newPhase);
  void StartPlay() { play_authorized_ = true; }
  void StopPlay() {
    play_authorized_ = false;
    clock_->StopBallInPlay();
  }
  void StartSetPiece() { set_piece_active_ = true; }
  void StopSetPiece() { set_piece_active_ = false; }
  void StartBallInPlay();
  void EndHalf() {
    StopPlay();
    StopSetPiece();
    clock_->EndHalf();
  }
  void SetBallRetainer(Player* retainer) { ball_retainer_ = retainer; }
  // Team-state write published by an actor through PlayerRuntimeSink. Runtime
  // owns the storage; the actor never touches TeamMatchState directly.
  void SetDesignatedPlayer(int team_id, Player* receiver) {
    team_states_[team_id].SetDesignatedPlayer(receiver);
  }
  void SetGoalScored(bool onOff) {
    if (onOff) clock_->StopBallInPlay();
    else ball_in_goal_ = false;
    goal_scored_ = onOff;
  }

  // Read-only runtime facts used by the composition root and diagnostics.
  football::sim::Tick GetTimelineTick() const { return clock_->now(); }
  football::sim::TickSpan GetRegulationTime() const { return clock_->RegulationTime(); }
  football::sim::TickSpan GetBallInPlayTime() const { return clock_->BallInPlayTime(); }
  const bool& IsHalfUnderway() const { return clock_->IsHalfUnderway(); }
  const bool& IsBallInPlay() const { return clock_->IsBallInPlay(); }
  football::ball::Ball* GetBall() const { return ball_.get(); }

  const football::pitch::Pitch& pitch() const { return pitch_; }
  const AnimationLibrary& GetAnimationLibrary() const { return *animations_; }
  const MatchOptions& options() const { return options_; }
  bool IsInSetPiece() const { return set_piece_active_; }
  MatchPhase GetMatchPhase() const { return phase_; }
  Referee* GetReferee() const { return referee_.get(); }
  // Runtime-owned restart execution facts for observation. Borrowed read-only
  // for the duration of one BuildWorldState/PlayerTick call.
  const football::sim::RestartContext& restart_context() const { return restarts_.context(); }
  // Derived control state; never a second authority over play/restart flags.
  football::sim::PlayState CurrentPlayState() const;
  int GetScore(int team_id) const { return score_[team_id]; }
  float GetPossessionFactor_60seconds() const { return possession_60_seconds_ / 60.0f; }
  std::uint64_t GetResetSequence() const { return reset_sequence_; }
  int FirstTeam() const { return first_team_; }
  int SecondTeam() const { return second_team_; }
  bool IsGoalScored() const { return goal_scored_; }
  bool IsBallInGoal() const { return ball_in_goal_; }
  int GetLastGoalTeam() const { return last_goal_team_; }
  Player* GetLastGoalScorer() const { return last_goal_scorer_; }
  Player* GetDesignatedPossessionPlayer() const { return designated_possession_player_; }
  Player* GetBallRetainer() const { return ball_retainer_; }
  std::optional<football::sim::TeamSide> GetBestPossessionTeam() const { return best_possession_team_; }
  const football::sim::event::TouchState& touches() const { return touches_; }
  int GetLastTouchTeamID() const { return touches_.last_team; }
  int GetLastTouchTeamID(e_TouchType touch_type) const {
    return touches_.last_team_by_type[touch_type];
  }
  int GetLastTouchTeam() const;
  Player* GetLastTouchPlayer() const;
  void GetActiveTeamPlayers(int team_id, std::vector<Player*>& players);

  // Constructed before the actors and kept alive until their borrows are gone.
  std::vector<MentalImage> mental_images_;
  std::optional<football::sim::MatchClock> clock_;
  football::sim::observation::SnapshotHistory snapshot_history_;
  football::sim::observation::SnapshotMetadata snapshot_metadata_;
  football::sim::observation::PlayerSlotTable player_slots_;
  football::sim::observation::Snapshot snapshot_scratch_;
  // Fixed home-then-away actor slots, including inactive/bench entries.
  std::vector<Player*> snapshot_players_;
  // Transient annotation only while StepImpl executes; absent for diagnostics.
  std::optional<std::uint64_t> snapshot_step_;

  MatchOptions options_;
  football::pitch::Pitch pitch_;
  football::ball::BallModel ball_config_;
  // Process-wide, immutable baked artifact: one parse serves every Simulation.
  std::shared_ptr<const AnimationLibrary> animations_;

  // Live team aggregates: definition plus the set of Players the Team owns.
  std::array<std::unique_ptr<football::team::Team>, 2> teams_;
  // The single authoritative ball in the Stadium Frame. Only the physics
  // kernel steps it; there is no processing-frame view or mirror.
  std::unique_ptr<football::ball::Ball> ball_;
  // Ball centre before the current tick's integration, exposed to the rules so
  // an out-of-play restart uses the exact crossing point.
  football::math::Vector3 referee_previous_ball_{};
  // Live match relationship state, separate from the owning Team.
  std::array<football::sim::ActiveLineupState, 2> active_lineups_;
  mutable std::array<football::sim::TeamOrientation, 2> team_orientations_;
  mutable std::array<football::sim::TeamMatchState, 2> team_states_;
  std::unique_ptr<Referee> referee_;
  football::sim::RestartController restarts_;

  std::unique_ptr<football::sim::rules::RuleCommandSink> rule_commands_;
  // Borrowed from rule_commands_; the concrete sink implements both ports.
  football::sim::PlayerRuntimeSink* player_runtime_sink_ = nullptr;
  // Single synchronous accepted-touch sink.
  std::unique_ptr<football::sim::AcceptedTouchSink> touch_sink_;
  // Synchronous send-off port; the concrete sink is a private nested class.
  std::unique_ptr<football::sim::PlayerSendOffSink> send_off_sink_;
  std::unique_ptr<football::sim::event::RulingSink> ruling_sink_;
  std::vector<football::sim::event::RefereeRuling> pending_rulings_;
  // Decision receipts only, not a second disciplinary or restart authority.
  std::vector<football::sim::event::CardRuling> committed_card_rulings_;
  std::vector<football::sim::FoulAssessment> foul_assessments_;
  football::sim::event::EventRecognizer recognizer_;
  football::sim::event::EventLog event_log_;
  // Read-only diagnostic log of accepted touches; shadow analysis only.
  std::vector<football::sim::event::RecordedTouch> recorded_touches_;

  // Stable dynamic-collider identity is Simulation-owned. Ball only sees an
  // opaque ColliderId; P4d will consume this mapping when it translates a
  // physical body impact into the existing touch/rules identity.
  std::map<football::geometry::ColliderId,
           std::pair<football::player::PlayerId, PlayerBodyPart>>
      body_collider_owners_;

  // P4b transient CCD input/evidence. It is deliberately outside BallState,
  // Snapshot and AcceptedTouch: the existing body-contact path stays authoritative.
  std::vector<PlayerBodyMotionPrediction> body_shadow_predictions_;
  std::vector<football::geometry::ColliderMotion> body_shadow_colliders_;
  std::optional<football::collision::CollisionHit> body_shadow_contact_;
  std::size_t body_shadow_touch_start_ = 0;
  bool body_shadow_active_ = false;
  bool body_shadow_action_phase_conflict_ = false;
  bool body_shadow_enabled_ = true; // internal diagnostic A/B gate, never policy
  PlayerBodyCollisionShadowReport body_shadow_report_;
  // Opt-in production-kernel shadow owner; no extra physics implementation.
  std::unique_ptr<football::ball::Ball> body_physics_shadow_ball_;
  std::vector<football::geometry::ColliderMotion> body_physics_shadow_colliders_;
  // The exact list the unified comparison used (upright, before any low-pose
  // proposal overwrites the shadow buffer). P4d-2 feeds this to production.
  std::vector<football::geometry::ColliderMotion> body_physics_production_colliders_;
  std::vector<BodyShadowCandidate> body_physics_shadow_candidates_;
  std::optional<BodyPhysicsShadowTick> body_physics_shadow_tick_, body_physics_shadow_latest_;
  BodyPhysicsShadowReport body_physics_shadow_report_;
  BodyContactEpisodes body_geometry_episodes_, body_impact_episodes_;
  std::unique_ptr<ActiveTouchShadowSink> active_touch_shadow_sink_;
  ActiveTouchShadowReport active_touch_shadow_report_;
  // Tick candidate buffer and arbitrated same-Tick endpoint impulse.
  std::vector<ActiveImpulseCandidate> tick_active_candidates_;
  std::optional<ActiveImpulseCandidate> pending_active_impulse_;
  ActiveTouchArbitration active_arbitration_;
  bool active_candidate_capture_ = false;
  bool active_touch_report_enabled_ = false;
  bool body_physics_production_ = false;
  bool prepared_tick_production_ = false;
  std::unique_ptr<football::ball::Ball> preparation_ball_;
  std::optional<football::ball::BallStepResult> committed_ball_tick_;
  std::vector<football::sim::event::RuleTouch> rule_touches_; // latest committed tick
  std::map<football::geometry::ColliderId, bool> rule_contact_episodes_;
  bool baked_body_production_ = false; // P4f-2 experiment; defaults stay OFF
  std::optional<std::uint64_t> baked_pose_generation_;
  std::vector<std::optional<PlayerBakedPoseSample>> baked_body_poses_, baked_body_starts_, baked_body_forecasts_;
  std::vector<football::geometry::ColliderMotion> baked_body_colliders_;

  // Competition / play / goal / touch state.
  football::sim::event::TouchState touches_;
  // Rule adapter handed to Player; it borrows the live touch/restart state.
  football::sim::PlayerAuthorizationPortImpl authorization_port_;
  int score_[2] = {0, 0};
  float possession_60_seconds_ = 0.0f;
  MatchPhase phase_ = MatchPhase::PreMatch;
  bool play_authorized_ = false;
  bool set_piece_active_ = false;
  bool goal_scored_ = false;
  bool ball_in_goal_ = false;
  int last_goal_team_ = -1;
  Player* last_goal_scorer_ = nullptr;
  Player* ball_retainer_ = nullptr;
  Player* designated_possession_player_ = nullptr;
  std::optional<football::sim::TeamSide> best_possession_team_;
  std::uint64_t reset_sequence_ = 0;
  bool pending_change_of_ends_ = false;
  football::sim::Tick last_body_ball_collision_tick_{};
  int first_team_ = 0;
  int second_team_ = 1;
};

#endif  // FOOTBALL_SIM_SIMULATION_HPP
