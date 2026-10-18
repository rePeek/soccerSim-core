#include "football/team/team.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace football::team {
namespace {
void Require(bool value, const char* message) {
  if (!value) throw std::invalid_argument(message);
}
// Formation templates are slot declarations. Explicit lineups may bind a small
// squad to sparse high slots; the legacy implicit declaration-order bound is
// enforced by Runtime's TeamSetup adapter, not by this static entity.
}  // namespace

void ValidateTeamDefinition(const TeamDefinition& team) {
  using football::player::kInvalidPlayerId;
  using football::player::PlayerId;
  std::set<PlayerId> ids;
  Require(!team.players.empty(), "team requires player definitions");
  for (const auto& player : team.players) {
    Require(player.id != kInvalidPlayerId, "invalid PlayerId");
    Require(ids.insert(player.id).second, "duplicate PlayerId");
  }
  Require(team.initial_formation.size() <= kFormationSlots &&
          team.tactical_formation.size() <= kFormationSlots,
          "formation exceeds slot count");
  const auto member = [&](PlayerId id) {
    Require(ids.contains(id), "unknown team PlayerId");
  };
  std::set<PlayerId> bound;
  for (const auto id : team.lineup.starters) {
    if (!id) continue;
    member(*id);
    Require(bound.insert(*id).second, "duplicate lineup player");
  }
  for (const auto id : team.lineup.substitutes) {
    member(id);
    Require(bound.insert(id).second, "duplicate lineup player");
  }
  const auto order = [&](const std::vector<PlayerId>& values) {
    std::set<PlayerId> seen;
    for (auto id : values) {
      member(id);
      Require(seen.insert(id).second, "duplicate role priority");
    }
  };
  if (team.leadership.captain) member(*team.leadership.captain);
  order(team.leadership.vice_captains);
  if (team.leadership.captain) {
    Require(std::ranges::find(team.leadership.vice_captains,
                              *team.leadership.captain) ==
                team.leadership.vice_captains.end(),
            "captain also vice captain");
  }
  const auto& o = team.set_piece_orders;
  for (const auto* list : {&o.penalties, &o.direct_free_kicks,
                           &o.indirect_free_kicks, &o.corners_left,
                           &o.corners_right, &o.throw_ins}) {
    order(*list);
  }
  ValidateFormationPresets(team.formations);
}

void ValidateTeamDefinitions(const TeamDefinition& home,
                               const TeamDefinition& away) {
  ValidateTeamDefinition(home);
  ValidateTeamDefinition(away);
  std::set<football::player::PlayerId> ids;
  for (const TeamDefinition* team : {&home, &away}) {
    for (const auto& player : team->players) {
      Require(ids.insert(player.id).second, "cross-team duplicate PlayerId");
    }
  }
}

std::optional<football::player::PlayerId> SelectEligibleTaker(
    std::span<const football::player::PlayerId> priority,
    std::span<const EligiblePlayer> candidates, SelectionFallback fallback) {
  using football::player::kInvalidPlayerId;
  for (auto id : priority) {
    for (const auto& candidate : candidates) {
      if (candidate.id == id && candidate.eligible && id != kInvalidPlayerId) {
        return id;
      }
    }
  }
  if (fallback == SelectionFallback::CandidateOrder) {
    for (const auto& candidate : candidates) {
      if (candidate.eligible && candidate.id != kInvalidPlayerId) {
        return candidate.id;
      }
    }
  }
  return std::nullopt;
}

std::optional<football::player::PlayerId> SelectCaptain(
    const Leadership& leadership, std::span<const EligiblePlayer> candidates,
    SelectionFallback fallback) {
  if (leadership.captain) {
    const std::array priority{*leadership.captain};
    if (auto id = SelectEligibleTaker(priority, candidates,
                                      SelectionFallback::None)) {
      return id;
    }
  }
  return SelectEligibleTaker(leadership.vice_captains, candidates, fallback);
}

}  // namespace football::team
