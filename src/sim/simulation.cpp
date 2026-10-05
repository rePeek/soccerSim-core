#include "sim/simulation.hpp"

#include <cassert>

#include "data/model_adapter.hpp"
#include "env/main.hpp"

#include "sim/match.hpp"
#include "sim/match_world_state.hpp"

Simulation::~Simulation() {
  Stop();
}

void Simulation::Init(
    const football::model::Team& home, const football::model::Team& away,
    const football::model::Pitch& pitch, const ScenarioConfig& scenario,
    bool init_animation) {
  assert(!match_);
  auto match_data = std::make_unique<MatchData>(
      ToTeamCreationData(home, kHomeTeamDatabaseId, scenario.left_team),
      ToTeamCreationData(away, kAwayTeamDatabaseId, scenario.right_team));

  // Profile construction historically consumes skin-colour draws before the
  // episode seed is applied. Seed here, before creating match actors, rather
  // than moving it ahead of MatchData construction in the environment.
  randomize(scenario.game_engine_random_seed);

  MatchOptions options;
  options.reverse_team_processing = scenario.reverse_team_processing;
  options.use_magnet = scenario.use_magnet;
  options.left_team_difficulty = scenario.left_team_difficulty;
  options.right_team_difficulty = scenario.right_team_difficulty;
  match_ = std::make_unique<Match>(std::move(match_data), pitch, options,
                                  init_animation);
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
