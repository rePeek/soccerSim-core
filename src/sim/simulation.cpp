#include "sim/simulation.hpp"

#include <cassert>

#include "sim/match.hpp"
#include "sim/match_world_state.hpp"

Simulation::~Simulation() {
  Stop();
}

void Simulation::Init(std::unique_ptr<MatchConfig> config,
                       const ControllerSet& controllers,
                       bool init_animation) {
  assert(config);
  assert(config->match_data);
  assert(!match_);
  match_ = std::make_unique<Match>(std::move(config->match_data), controllers,
                                   *config, init_animation);
}

void Simulation::Step(const PlayerControlSet& controls) {
  assert(match_);
  match_->Step(controls);
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

WorldState Simulation::Observe() const {
  assert(match_);
  return BuildWorldState(*match_);
}


bool Simulation::Stop() {
  if (!match_) return false;
  match_->Exit();
  match_.reset();
  return true;
}
