#ifndef FOOTBALL_SIM_SIMULATION_HPP
#define FOOTBALL_SIM_SIMULATION_HPP

#include <memory>

#include "control/player_control_set.hpp"
#include "model/team.hpp"
#include "model/pitch.hpp"
#include "sim/match_options.hpp"
#include "sim/rng.hpp"
#include "observation/world_state.hpp"

// Owns the match lifecycle, the deterministic simulation RNG and the baked
// animation library. Initialization preserves profile creation / RNG seeding
// order. Controllers are not part of this boundary: players read
// PlayerControlSet, and the RNG reaches actors through Match.
class Match;
class AnimationLibrary;
class LegacyPlayerDecisionFactory;
class LegacyTeamDecisionFactory;
class Simulation {
 public:
  // Seeds the pre-match RNG state, exactly as the legacy startup did.
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

  Match* match() { return match_.get(); }
  const Match* match() const { return match_.get(); }

 private:
  void EnsureAnimationLibrary();

  blunted::SimulationRng rng_;
  std::unique_ptr<Match> match_;
  std::shared_ptr<AnimationLibrary> animations_;
  // TRANSITIONAL (A mode): constructing the default decision factory here is a
  // convenience. It moves to the composition root once the decision code lives
  // outside simulation.
  std::shared_ptr<const LegacyPlayerDecisionFactory> player_decision_factory_;
  std::shared_ptr<const LegacyTeamDecisionFactory> team_decision_factory_;
};

#endif  // FOOTBALL_SIM_SIMULATION_HPP
