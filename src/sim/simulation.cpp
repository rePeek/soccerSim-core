#include "sim/simulation.hpp"

#include <cassert>

#include "sim/match.hpp"

Simulation::~Simulation() {
  Stop();
}

void Simulation::Reset(std::unique_ptr<MatchConfig> config,
                       const std::vector<AIControlledKeyboard*>& controllers,
                       bool init_animation) {
  assert(config);
  assert(config->match_data);
  assert(!match_);
  match_ = std::make_unique<Match>(std::move(config->match_data), controllers,
                                   *config, init_animation);
}

bool Simulation::Stop() {
  if (!match_) return false;
  match_->Exit();
  match_.reset();
  return true;
}
