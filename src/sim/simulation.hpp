#ifndef FOOTBALL_SIM_SIMULATION_HPP
#define FOOTBALL_SIM_SIMULATION_HPP

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
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
#include "sim/runtime/result.hpp"
#include "sim/runtime/clock.hpp"
#include "sim/fact/simulation_fact_sink.hpp"
#include "sim/event/event_recognizer.hpp"
#include "sim/event/touch_state.hpp"
#include "football/ball/ball_environment.hpp"
#include "sim/referee/referee_tick_facts.hpp"
#include "sim/referee/rule_command_sink.hpp"
#include "sim/player/player_tick_context.hpp"
#include "sim/player/player_runtime_sink.hpp"
#include "sim/fact/simulation_fact.hpp"
#include "sim/fact/tick_fact_buffer.hpp"
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
  class FactEvents;
  class RulingEvents;

  // Single fact production path: every contact consequence is buffered, then
  // consumed, so the legacy synchronous rule-command order is preserved while
  // the boundary between physics and rules is one immutable fact stream.
  Player* FindPlayerById(football::model::PlayerId id) const;
  void EmitFact(football::sim::event::SimulationFact fact);
  void FlushFacts();
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
  void CaptureMentalImage();
  void StepImpl(const PlayerControlSet& controls);
  void CaptureSnapshot();
  void EndPeriod();
  void ApplyChangeOfEnds();
  void UpdateRecentPossession(football::sim::TickSpan admitted);

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
  football::sim::observation::Snapshot snapshot_scratch_;
  // Fixed home-then-away actor slots, including inactive/bench entries.
  std::vector<Player*> snapshot_players_;

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
  // Single write-only fact sink for ball touches and player trips.
  std::unique_ptr<football::sim::SimulationFactSink> fact_sink_;
  std::unique_ptr<RulingEvents> ruling_sink_;
  football::sim::event::TickFactBuffer facts_;
  std::vector<football::sim::event::RefereeRuling> pending_rulings_;
  football::sim::event::EventRecognizer recognizer_;
  football::sim::event::EventLog event_log_;
  bool flushing_facts_ = false;

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
