#include "sim/simulation.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <vector>

#include "sim/animation/library.hpp"



#include "sim/match.hpp"
#include "sim/match_world_state.hpp"

namespace {

std::vector<FormationEntry> ToLegacyFormation(const football::model::Formation& model) {
  std::vector<FormationEntry> result;
  result.reserve(model.size());
  for (const auto& entry : model) {
    result.emplace_back(entry.position.x, entry.position.y, entry.role,
                        entry.lazy, entry.controllable);
  }
  return result;
}

// Resolve only unspecified runtime appearance, never import or invent profiles.
// Preserve the historical home-then-away draw for every declared profile, even
// profiles omitted by a smaller formation and profiles with explicit appearance.
void ResolveAppearance(football::model::Team& team, blunted::Rng& rng) {
  for (auto& player : team.players) {
    const int skin_color = int(std::round(rng.Uniform(1, 4)));
    if (!player.appearance.skin_color) player.appearance.skin_color = skin_color;
  }
}

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

// Validate the effective descriptions before profile draws, reseeding or
// actor construction. Caller identities are never generated or renumbered.
void ValidatePlayers(const football::model::Team& home, const football::model::Team& away) {
  if (home.players.empty() || away.players.empty()) {
    throw std::invalid_argument("match requires explicit player rosters");
  }
  const auto player_count = [](const football::model::Team& team) -> std::size_t {
    return !team.formation.empty() ? team.formation.size()
        : !team.tactical_formation.empty() ? team.tactical_formation.size()
        : team.players.size();
  };
  const std::size_t home_count = player_count(home);
  const std::size_t away_count = player_count(away);
  if (home_count > home.players.size() || away_count > away.players.size()) {
    throw std::invalid_argument("formation has no corresponding player profile");
  }
  std::set<football::model::PlayerId> identities;
  for (const football::model::Team* team : {&home, &away}) {
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
    const football::model::Pitch& pitch, MatchOptions options) {
  if (match_) throw std::logic_error("simulation already initialized");
  // Reject non-advancing/non-finite clocks before any appearance/RNG draws.
  const float factor = options.match_duration * 0.2f + 0.05f;
  if (!std::isfinite(options.match_duration) || options.match_duration < 0.0f ||
      !std::isfinite(factor) || 10.0f * (1.0f / factor) < 1.0f ||
      options.half_duration_ms == 0 ||
      options.half_duration_ms > (std::numeric_limits<std::uint64_t>::max() - 200) / 2) {
    throw std::invalid_argument("invalid regulation duration or match clock scale");
  }
  ValidatePlayers(home, away);
  const std::vector<FormationEntry> left = ToLegacyFormation(home.formation);
  const std::vector<FormationEntry> right = ToLegacyFormation(away.formation);
  football::model::Team home_model = home, away_model = away;
  ResolveAppearance(home_model, rng_);
  ResolveAppearance(away_model, rng_);

  // Derive the kickoff rule once; input selection is not simulation state.
  options.left_team_owns_ball =
      LeftTeamOwnsBall(left, right, options.ball_position);

  // Apply the episode seed after the appearance draws, before constructing
  // actors. Moving those draws across this boundary changes simulation RNG.
  rng_.Seed(options.game_engine_random_seed);

  EnsureAnimationLibrary();
  match_ = std::make_unique<Match>(home_model, away_model, pitch, options, rng_,
                                  animations_);
}

void Simulation::EnsureAnimationLibrary() {
  if (animations_) return;
  // Never put resource loading inside assert(): Release must load and reject
  // invalid resources too. Publish only a successfully loaded library.
  auto animations = std::make_shared<AnimationLibrary>();
  if (!animations->Load(GFOOTBALL_BAKED_ANIM_PATH)) {
    throw std::runtime_error("Simulation: cannot load baked animations: "
                             GFOOTBALL_BAKED_ANIM_PATH);
  }
  animations_ = std::move(animations);
}

void Simulation::Step(const PlayerControlSet& controls) {
  if (!match_) throw std::logic_error("simulation has no match");
  match_->Step(controls);
}



bool Simulation::IsInPlay() const {
  return match_ && match_->IsInPlay();
}

WorldState Simulation::Observe() const {
  if (!match_) throw std::logic_error("simulation has no match");
  return BuildWorldState(*match_);
}

bool Simulation::Finished() const {
  return match_ && match_->Finished();
}

MatchResult Simulation::Result() const {
  if (!match_) throw std::logic_error("simulation has no final result");
  return match_->Result();
}

bool Simulation::Stop() {
  if (!match_) return false;
  match_->Exit();
  match_.reset();
  return true;
}
