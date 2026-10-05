#ifndef FOOTBALL_SIM_SIMULATION_HPP
#define FOOTBALL_SIM_SIMULATION_HPP

#include <memory>

#include "controller/controller_set.hpp"
#include "model/team.hpp"
#include "model/pitch.hpp"
#include "control/player_control_set.hpp"
#include "state/world_state.hpp"

class Match;
class EnvState;
class SharedInfo;
struct ScenarioConfig;

// Owns match lifecycle and runtime construction. The legacy episode input is
// explicit; initialization preserves profile creation / RNG seeding order.
class Simulation {
 public:
  Simulation() = default;
  ~Simulation();

  void Init(const football::model::Team& home,
            const football::model::Team& away,
            const football::model::Pitch& pitch,
            const ScenarioConfig& scenario, const ControllerSet& controllers,
            bool init_animation,
            const std::vector<ControllerAssignment>& assignments = {});
  bool Stop();
  void Step(const PlayerControlSet& controls);
  void ProcessState(EnvState* state);
  void GetState(SharedInfo* state);
  bool IsInPlay() const;
  WorldState Observe() const;

  Match* match() { return match_.get(); }
  const Match* match() const { return match_.get(); }

 private:
  std::unique_ptr<Match> match_;
};

#endif  // FOOTBALL_SIM_SIMULATION_HPP
