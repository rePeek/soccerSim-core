#ifndef FOOTBALL_SIM_SIMULATION_HPP
#define FOOTBALL_SIM_SIMULATION_HPP

#include <memory>
#include <vector>

#include "sim/match_config.hpp"

class AIControlledKeyboard;
class Match;
class EnvState;
class SharedInfo;

// Owns match lifecycle and advances the authoritative simulation. The
// composition root supplies controller devices and performs legacy RNG setup.
class Simulation {
 public:
  Simulation() = default;
  ~Simulation();

  void Reset(std::unique_ptr<MatchConfig> config,
             const std::vector<AIControlledKeyboard*>& controllers,
             bool init_animation);
  bool Stop();
  void Step();
  void ProcessState(EnvState* state);
  void GetState(SharedInfo* state);
  bool IsInPlay() const;

  Match* match() { return match_.get(); }
  const Match* match() const { return match_.get(); }

  // Compatibility accessor for legacy internal callers.
  Match* GetMatch() { return match(); }

 private:
  std::unique_ptr<Match> match_;
};

#endif  // FOOTBALL_SIM_SIMULATION_HPP
