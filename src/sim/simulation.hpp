#ifndef FOOTBALL_SIM_SIMULATION_HPP
#define FOOTBALL_SIM_SIMULATION_HPP

#include <chrono>
#include <memory>
#include <vector>

#include "sim/player/player_control_set.hpp"
#include "model/team.hpp"
#include "model/pitch.hpp"
#include "sim/match/match_options.hpp"
#include "sim/random/rng.hpp"
#include "sim/observation/world_state.hpp"
#include "sim/observation/mentalimage.hpp"
#include "sim/match/match_result.hpp"
#include "sim/match/match_clock.hpp"
#include <optional>
#include "sim/event/ball_touch_sink.hpp"
#include "sim/rules/referee_tick_facts.hpp"
#include "sim/rules/rule_command_sink.hpp"
#include "sim/player/player_tick_context.hpp"
#include "sim/player/player_runtime_sink.hpp"

class Match;
class Player;
class AnimationLibrary;
class Referee;
namespace football::sim::testing { class SimulationAccess; }

// Simulation knows controls, rules, runtime history and execution, not AI
// objects or factories. Composition owns all decisions outside this boundary.
class Simulation {
 public:
  Simulation();
  ~Simulation();

  void Init(const football::model::Team& home,
            const football::model::Team& away,
            const football::model::Pitch& pitch, MatchOptions options);
  bool Stop();
  void Step(const PlayerControlSet& controls);
  bool IsInPlay() const;
  WorldState Observe() const;
  bool Finished() const;
  // Final only; throws std::logic_error before full time or without a match.
  MatchResult Result() const;

  // TODO: test/diagnostic escape hatch; remove from the public Simulation API.
  // Transitional access to authoritative actors, not a general integration API.
  Match* match() { return match_.get(); }
  const Match* match() const { return match_.get(); }

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
  void PublishBallTouch(const football::sim::BallTouchEvent& event);
  football::sim::rules::RefereeTickFacts RefereeFacts() const;
  football::sim::PlayerTickContext PlayerTickFacts(const Player& actor) const;
  void EnsureAnimationLibrary();
  void CaptureMentalImage(Match& match);
  void EndPeriod(Match& match);
  void ApplyChangeOfEnds(Match& match);
  void UpdateRecentPossession(Match& match, football::sim::TickSpan admitted);

  blunted::SimulationRng rng_;
  // Constructed before Match and kept alive until its borrowed references are gone.
  std::vector<MentalImage> mental_images_;
  std::optional<football::sim::MatchClock> clock_;
  std::unique_ptr<Match> match_;
  std::unique_ptr<Referee> referee_;
  std::unique_ptr<football::sim::rules::RuleCommandSink> rule_commands_;
  // Borrowed from rule_commands_; the concrete sink implements both ports.
  football::sim::PlayerRuntimeSink* player_runtime_sink_ = nullptr;
  // Runtime touch publication; keeps the write-only sink out of actors.
  std::unique_ptr<football::sim::BallTouchSink> touch_sink_;
  std::shared_ptr<AnimationLibrary> animations_;
};

#endif  // FOOTBALL_SIM_SIMULATION_HPP
