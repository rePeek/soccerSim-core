#ifndef FOOTBALL_SIM_SIMULATION_HPP
#define FOOTBALL_SIM_SIMULATION_HPP

#include <memory>

#include "control/player_control_set.hpp"
#include "model/team.hpp"
#include "model/pitch.hpp"
#include "sim/match_options.hpp"
#include "state/world_state.hpp"

class Match;
class AnimationLibrary;

// Owns match lifecycle, the baked animation library and runtime construction.
// Initialization preserves profile creation / RNG seeding order.
// Controllers are not part of this boundary: players read PlayerControlSet.
class Simulation {
 public:
  Simulation() = default;
  ~Simulation();

  void Init(const football::model::Team& home,
            const football::model::Team& away,
            const football::model::Pitch& pitch, MatchOptions options,
            bool init_animation);
  bool Stop();
  void Step(const PlayerControlSet& controls);
  bool IsInPlay() const;
  WorldState Observe() const;

  Match* match() { return match_.get(); }
  const Match* match() const { return match_.get(); }

 private:
  void EnsureAnimationLibrary();

  std::unique_ptr<Match> match_;
  std::shared_ptr<AnimationLibrary> animations_;
};

#endif  // FOOTBALL_SIM_SIMULATION_HPP
