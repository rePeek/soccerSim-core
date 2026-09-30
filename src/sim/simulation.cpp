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

void Simulation::Step() {
  assert(match_);
  match_->Step();
}

void Simulation::ProcessState(EnvState* state) {
  assert(match_);
  match_->ProcessState(state);
}

void Simulation::GetState(SharedInfo* state) {
  assert(match_);
  match_->GetState(state);
}

bool Simulation::IsInPlay() const {
  return match_ && match_->IsInPlay();
}

bool Simulation::Stop() {
  if (!match_) return false;
  match_->Exit();
  match_.reset();
  return true;
}
