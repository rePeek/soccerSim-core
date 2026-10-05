#include "sim/simulation.hpp"

#include <algorithm>
#include <cassert>
#include <set>
#include <stdexcept>
#include <vector>

#include "animation/library.hpp"

#include "data/model_adapter.hpp"
#include "support/diagnostics/log.hpp"

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

// Validate the effective descriptions before profile draws, reseeding or
// actor construction. Caller identities are never generated or renumbered.
void ValidatePlayers(const TeamCreationData& home, const TeamCreationData& away) {
  const auto player_count = [](const TeamCreationData& team) -> std::size_t {
    return team.formation.empty() ? kPlayersPerTeam : team.formation.size();
  };
  const std::size_t home_count = player_count(home);
  const std::size_t away_count = player_count(away);
  if (home_count > home.players.size() || away_count > away.players.size()) {
    throw std::invalid_argument("formation has no corresponding player profile");
  }
  std::set<football::model::PlayerId> identities;
  for (const TeamCreationData* team : {&home, &away}) {
    for (const football::model::Player& player : team->players) {
      if (player.id == football::model::kInvalidPlayerId) {
        throw std::invalid_argument("match player requires an explicit PlayerId");
      }
      if (!identities.insert(player.id).second) {
        throw std::invalid_argument("duplicate PlayerId across match rosters");
      }
    }
  }
}

}  // namespace

Simulation::Simulation() {
  // The pre-match profile draws historically ran on an RNG freshly seeded with
  // 0, before the episode seed was applied in Init(). Keep that exact window.
  rng_.Seed(0);
}

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
  const TeamCreationData home_data =
      ToTeamCreationData(home, kHomeTeamDatabaseId, left);
  const TeamCreationData away_data =
      ToTeamCreationData(away, kAwayTeamDatabaseId, right);
  ValidatePlayers(home_data, away_data);
  auto match_data = std::make_unique<MatchData>(home_data, away_data, rng_);

  // Derived once here, so the referee and team selection never read ambient
  // configuration during a tick.
  options.left_team_owns_ball =
      LeftTeamOwnsBall(left, right, options.ball_position);
  options.dynamic_player_selection = DynamicPlayerSelection(left, right);

  // Profile construction consumes skin-colour draws before the match seed is
  // applied. Reseed here, before creating match actors, rather than moving it
  // ahead of the MatchData draws above.
  rng_.Seed(options.game_engine_random_seed);

  EnsureAnimationLibrary();
  match_ = std::make_unique<Match>(std::move(match_data), pitch, options, rng_,
                                  animations_, init_animation);
}

void Simulation::EnsureAnimationLibrary() {
  if (animations_) return;
  animations_ = std::make_shared<AnimationLibrary>();
  // Deliberately not assert()/CHECK(): both compile to ((void)0) under NDEBUG,
  // which would silently skip the load and leave an empty library behind.
  if (!animations_->Load(GFOOTBALL_BAKED_ANIM_PATH)) {
    Log(blunted::e_FatalError, "Simulation", "Init",
        "cannot load baked animations");
  }
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
