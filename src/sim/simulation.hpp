#ifndef FOOTBALL_SIM_SIMULATION_HPP
#define FOOTBALL_SIM_SIMULATION_HPP

#include <memory>

#include "sim/player/player_control_set.hpp"
#include "model/team.hpp"
#include "model/pitch.hpp"
#include "sim/match/match_options.hpp"
#include "sim/random/rng.hpp"
#include "sim/observation/world_state.hpp"
#include "sim/match/match_result.hpp"

class Match;
class AnimationLibrary;

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

  Match* match() { return match_.get(); }
  const Match* match() const { return match_.get(); }

 private:
  void EnsureAnimationLibrary();

  blunted::SimulationRng rng_;
  std::unique_ptr<Match> match_;
  std::shared_ptr<AnimationLibrary> animations_;
};

#endif  // FOOTBALL_SIM_SIMULATION_HPP
