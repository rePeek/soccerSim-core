#ifndef FOOTBALL_SIM_SIMULATION_HPP
#define FOOTBALL_SIM_SIMULATION_HPP

#include <array>
#include <memory>

#include "control/player_control_set.hpp"
#include "control/tactical_board.hpp"
#include "model/team.hpp"
#include "model/pitch.hpp"
#include "sim/match_options.hpp"
#include "sim/rng.hpp"
#include "observation/world_state.hpp"

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
            const football::model::Pitch& pitch, MatchOptions options,
            bool init_animation);
  bool Stop();
  void Step(const PlayerControlSet& controls);
  bool IsInPlay() const;
  WorldState Observe() const;
  std::array<TacticalBoard, 2> ObserveTactics() const;

  Match* match() { return match_.get(); }
  const Match* match() const { return match_.get(); }

 private:
  void EnsureAnimationLibrary();

  blunted::SimulationRng rng_;
  std::unique_ptr<Match> match_;
  std::shared_ptr<AnimationLibrary> animations_;
};

#endif  // FOOTBALL_SIM_SIMULATION_HPP
