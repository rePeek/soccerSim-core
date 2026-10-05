#include "sim/simulation.hpp"

#include <algorithm>
#include <cassert>
#include <vector>

#include "data/model_adapter.hpp"
#include "env/main.hpp"

#include "sim/match.hpp"
#include "sim/match_world_state.hpp"

namespace {

// Mirrors the retired ScenarioConfig::LeftTeamOwnsBall(): the team whose nearest
// initial formation player stands closer to the kickoff spot starts in possession.
bool LeftTeamOwnsBall(const std::vector<FormationEntry>& left,
                      const std::vector<FormationEntry>& right,
                      const blunted::Vector3& ball_position) {
  float leftDistance = 1000000;
  float rightDistance = 1000000;
  for (const FormationEntry& player : left) {
    leftDistance = std::min(
        leftDistance, (player.start_position - ball_position).GetLength());
  }
  for (const FormationEntry& player : right) {
    rightDistance = std::min(
        rightDistance, (player.start_position - ball_position).GetLength());
  }
  return leftDistance < rightDistance;
}

// Mirrors the retired ScenarioConfig::DynamicPlayerSelection() with the legacy
// agent defaults: one controllable left agent and none on the right.
bool DynamicPlayerSelection(const std::vector<FormationEntry>& left,
                            const std::vector<FormationEntry>& right) {
  constexpr int kLeftAgents = 1;
  constexpr int kRightAgents = 0;
  int controllable_left = 0;
  int controllable_right = 0;
  for (const FormationEntry& entry : left) {
    if (entry.controllable) ++controllable_left;
  }
  for (const FormationEntry& entry : right) {
    if (entry.controllable) ++controllable_right;
  }
  return !((controllable_left == kLeftAgents || kLeftAgents == 0) &&
           (controllable_right == kRightAgents || kRightAgents == 0));
}

}  // namespace
Simulation::~Simulation() {
  Stop();
}

void Simulation::Init(
    const football::model::Team& home, const football::model::Team& away,
    const football::model::Pitch& pitch, MatchOptions options,
    bool init_animation) {
  assert(!match_);
  // Effective initial formations come from the model teams; there is no
  // ambient episode override any more.
  const std::vector<FormationEntry> left = ToLegacyFormation(home.formation);
  const std::vector<FormationEntry> right = ToLegacyFormation(away.formation);
  auto match_data = std::make_unique<MatchData>(
      ToTeamCreationData(home, kHomeTeamDatabaseId, left),
      ToTeamCreationData(away, kAwayTeamDatabaseId, right));

  // Derived once here, so the referee and team selection never read ambient
  // configuration during a tick.
  options.left_team_owns_ball =
      LeftTeamOwnsBall(left, right, options.ball_position);
  options.dynamic_player_selection = DynamicPlayerSelection(left, right);

  // Profile construction historically consumes skin-colour draws before the
  // episode seed is applied. Seed here, before creating match actors, rather
  // than moving it ahead of MatchData construction in the environment.
  randomize(options.game_engine_random_seed);

  match_ = std::make_unique<Match>(std::move(match_data), pitch, options,
                                  init_animation);
}

void Simulation::Step(const PlayerControlSet& controls) {
  assert(match_);
  match_->Step(controls);
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
