#ifndef FOOTBALL_SIM_SIMULATION_HPP
#define FOOTBALL_SIM_SIMULATION_HPP

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <map>
#include <vector>

#include "sim/player/player_control_set.hpp"
#include "model/team.hpp"
#include "model/pitch.hpp"
#include "model/ball_config.hpp"
#include "sim/simulation_config.hpp"
#include "foundation/math/rng.hpp"
#include "sim/observation/world_state.hpp"
#include "sim/observation/mentalimage.hpp"
#include "sim/observation/snapshot_history.hpp"
#include "sim/observation/player_slots.hpp"
#include "sim/runtime/result.hpp"
#include "sim/runtime/clock.hpp"
#include "sim/event/accepted_touch_sink.hpp"
#include "sim/event/event_recognizer.hpp"
#include "sim/event/touch_state.hpp"
#include "sim/event/touch_record.hpp"
#include "sim/event/rule_touch.hpp"
#include "football/ball/ball_environment.hpp"
#include "sim/referee/referee_tick_facts.hpp"
#include "sim/referee/rule_command_sink.hpp"
#include "sim/player/player_tick_context.hpp"
#include "sim/player/player_runtime_sink.hpp"
#include "sim/player/foul_assessment.hpp"
#include "sim/player/player_body_collision_shadow.hpp"
#include "sim/player/player_active_touch_shadow.hpp"

#include "sim/referee/ruling.hpp"
#include "sim/event/match_event.hpp"
#include "sim/event/event_log.hpp"
#include "sim/referee/referee_view.hpp"

class Player;
namespace football::ball { class Ball; }
class Team;
class Referee;
class AnimationLibrary;
namespace football::sim::testing { class SimulationAccess; }

// Simulation owns the whole match composition: clock/phase/score, ball, rosters,
// referee, touch/possession state, RNG/history/config. Step() is the only
// orchestration point. Actors never hold a Simulation pointer.
class Simulation {
 public:
  Simulation();
  ~Simulation();

  void Init(const football::model::Team& home,
            const football::model::Team& away,
            const football::model::Pitch& pitch, MatchOptions options,
            const football::model::BallConfig& ball_config = football::model::BallConfig{});
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
  // Whole-step summaries published with the latest Snapshot, not contact facts.
  const std::vector<football::sim::event::EventTrajectory>& EventTrajectories() const {
    return recognizer_.trajectories();
  }

  // TODO: test/diagnostic escape hatch; not a general integration API.
  // Transitional test/diagnostic sampling; pointers expire on capture/reset/Stop.
  MentalImage* GetMentalImage(football::sim::TickSpan history);
  MentalImage* GetMentalImage(std::chrono::milliseconds history);
  // Transitional test/diagnostic clock-only advance: no actors, rules or Step count.
  void AdvanceTime(football::sim::TickSpan delta);
  // Runtime composition, also transitional test/diagnostic escapes.
  void Mirror(bool team_0, bool team_1, bool ball);
  void ResetSituation(const blunted::Vector3& focus_position);
  // Transitional diagnostic impulse; synchronous physics/history/possession only.
  void TouchBall(const blunted::Vector3& impulse);

 private:
  friend class football::sim::testing::SimulationAccess;
  class RuleCommands;
  class TouchEvents;
  class RulingEvents;
  class ActiveTouchEvents;

  // Synchronous accepted-touch dispatch: bookkeeping, recognition and the
  // referee consume happen at the exact legacy boundary and in legacy order.
  Player* FindPlayerById(football::model::PlayerId id) const;
  void DispatchTouch(const football::sim::event::AcceptedTouch& touch);
  void ApplyPendingRulings();
  // Exhaustive ruling execution: every defined verdict has an explicit handler,
  // so a new ruling type cannot be silently dropped by the executor.
  void ApplyGoalRuling(const football::sim::event::AwardGoalRuling& ruling);
  void ApplyRestartRuling(const football::sim::event::AwardRestartRuling& ruling);
  void ApplyCardRuling(const football::sim::event::CardRuling& ruling);
  void ProcessReferee();
  // Recognized behavior transitions -> confirmed MatchEvents.
  void CommitEventTransitions(football::sim::Tick now);
  void AdvanceReferee(const football::sim::rules::RefereeView& view);
  football::sim::rules::RefereeTickFacts RefereeFacts() const;
  football::sim::PlayerTickContext PlayerTickFacts(const Player& actor);
  void EnsureAnimationLibrary();
  void CaptureMentalImage(const football::ball::Ball* view = nullptr);
  void StepImpl(const PlayerControlSet& controls);
  void CaptureSnapshot();
  void EndPeriod();
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
  // Explicit timing-migration gate, independent of all diagnostic sinks.
  void EnablePreparedTickProduction(bool enabled);
  void RunPreparedPlayerTick();
  void PublishRuleTouch(football::sim::event::RuleTouch touch);
  void PublishBodyRuleTouches(const football::ball::BallStepResult& result,
      std::span<const football::ball::ColliderMotion> bodies);

  // Lifecycle mutations, published through the write-only rule/player ports.
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
  Team* GetTeam(int team_id) const { return teams_[team_id].get(); }
  const football::model::Pitch& pitch() const { return pitch_; }
  const AnimationLibrary& GetAnimationLibrary() const { return *animations_; }
  const MatchOptions& options() const { return options_; }
  blunted::Rng& rng() { return rng_; }
  bool IsInSetPiece() const { return set_piece_active_; }
  MatchPhase GetMatchPhase() const { return phase_; }
  Referee* GetReferee() const { return referee_.get(); }
  int GetScore(int team_id) const { return score_[team_id]; }
  float GetPossessionFactor_60seconds() const { return possession_60_seconds_ / 60.0f; }
  std::uint64_t GetResetSequence() const { return reset_sequence_; }
  int FirstTeam() const { return first_team_; }
  int SecondTeam() const { return second_team_; }
  bool isBallMirrored() const { return ball_mirrored_; }
  bool IsGoalScored() const { return goal_scored_; }
  bool IsBallInGoal() const { return ball_in_goal_; }
  Team* GetLastGoalTeam() const { return last_goal_team_; }
  Player* GetLastGoalScorer() const { return last_goal_scorer_; }
  Player* GetDesignatedPossessionPlayer() const { return designated_possession_player_; }
  Player* GetBallRetainer() const { return ball_retainer_; }
  Team* GetBestPossessionTeam() const { return best_possession_team_; }
  const football::sim::event::TouchState& touches() const { return touches_; }
  int GetLastTouchTeamID() const { return touches_.last_team; }
  int GetLastTouchTeamID(e_TouchType touch_type) const {
    return touches_.last_team_by_type[touch_type];
  }
  Team* GetLastTouchTeam() const;
  Player* GetLastTouchPlayer() const;
  void GetActiveTeamPlayers(int team_id, std::vector<Player*>& players);
  football::ball::BallEnvironment GetBallEnvironment() const { return {ball_in_goal_}; }

  blunted::Rng rng_;
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
  football::model::Pitch pitch_;
  football::model::BallConfig ball_config_;
  std::shared_ptr<AnimationLibrary> animations_;

  std::unique_ptr<football::ball::Ball> ball_;
  std::array<std::unique_ptr<Team>, 2> teams_;
  std::unique_ptr<Referee> referee_;

  std::unique_ptr<football::sim::rules::RuleCommandSink> rule_commands_;
  // Borrowed from rule_commands_; the concrete sink implements both ports.
  football::sim::PlayerRuntimeSink* player_runtime_sink_ = nullptr;
  // Single synchronous accepted-touch sink.
  std::unique_ptr<football::sim::AcceptedTouchSink> touch_sink_;
  std::unique_ptr<RulingEvents> ruling_sink_;
  std::vector<football::sim::event::RefereeRuling> pending_rulings_;
  std::vector<football::sim::FoulAssessment> foul_assessments_;
  football::sim::event::EventRecognizer recognizer_;
  football::sim::event::EventLog event_log_;
  // Read-only diagnostic log of accepted touches; shadow analysis only.
  std::vector<football::sim::event::RecordedTouch> recorded_touches_;

  // Stable dynamic-collider identity is Simulation-owned. Ball only sees an
  // opaque ColliderId; P4d will consume this mapping when it translates a
  // physical body impact into the existing touch/rules identity.
  std::map<football::ball::ColliderId,
           std::pair<football::model::PlayerId, PlayerBodyPart>>
      body_collider_owners_;

  // P4b transient CCD input/evidence. It is deliberately outside BallState,
  // Snapshot and AcceptedTouch: the existing body-contact path stays authoritative.
  std::vector<PlayerBodyMotionPrediction> body_shadow_predictions_;
  std::vector<football::ball::ColliderMotion> body_shadow_colliders_;
  std::optional<football::ball::BallContact> body_shadow_contact_;
  std::size_t body_shadow_touch_start_ = 0;
  bool body_shadow_active_ = false;
  bool body_shadow_action_phase_conflict_ = false;
  bool body_shadow_enabled_ = true; // internal diagnostic A/B gate, never policy
  PlayerBodyCollisionShadowReport body_shadow_report_;
  // Opt-in production-kernel shadow owner; no extra physics implementation.
  std::unique_ptr<football::ball::Ball> body_physics_shadow_ball_;
  std::vector<football::ball::ColliderMotion> body_physics_shadow_colliders_;
  // The exact list the unified comparison used (upright, before any low-pose
  // proposal overwrites the shadow buffer). P4d-2 feeds this to production.
  std::vector<football::ball::ColliderMotion> body_physics_production_colliders_;
  std::vector<BodyShadowCandidate> body_physics_shadow_candidates_;
  std::optional<BodyPhysicsShadowTick> body_physics_shadow_tick_, body_physics_shadow_latest_;
  BodyPhysicsShadowReport body_physics_shadow_report_;
  BodyContactEpisodes body_geometry_episodes_, body_impact_episodes_;
  std::unique_ptr<ActiveTouchShadowSink> active_touch_shadow_sink_;
  ActiveTouchShadowReport active_touch_shadow_report_;
  bool active_impulse_production_ = false;
  // P5e: real tick candidate buffer and the arbitrated winner. The winner is the
  // value P5e-2 will submit through BallTickInput::active_impulse.
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
  std::map<football::ball::ColliderId, bool> rule_contact_episodes_;

  // Competition / play / goal / touch state.
  football::sim::event::TouchState touches_;
  int score_[2] = {0, 0};
  float possession_60_seconds_ = 0.0f;
  MatchPhase phase_ = MatchPhase::PreMatch;
  bool play_authorized_ = false;
  bool set_piece_active_ = false;
  bool goal_scored_ = false;
  bool ball_in_goal_ = false;
  Team* last_goal_team_ = nullptr;
  Player* last_goal_scorer_ = nullptr;
  Player* ball_retainer_ = nullptr;
  Player* designated_possession_player_ = nullptr;
  Team* best_possession_team_ = nullptr;
  std::uint64_t reset_sequence_ = 0;
  bool pending_change_of_ends_ = false;
  football::sim::Tick last_body_ball_collision_tick_{};
  int first_team_ = 0;
  int second_team_ = 1;
  bool ball_mirrored_ = false;
};

#endif  // FOOTBALL_SIM_SIMULATION_HPP
