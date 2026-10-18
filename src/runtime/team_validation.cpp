#include "football/runtime/team.hpp"
#include "football/runtime/match_config.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace football::sim {
using football::player::PlayerId;
using football::player::kInvalidPlayerId;
namespace {
void Require(bool value, const char* message) {
  if (!value) throw std::invalid_argument(message);
}
std::size_t FormationCount(const TeamSetup& s) {
  return !s.formation.empty() ? s.formation.size() :
      !s.tactical_formation.empty() ? s.tactical_formation.size() : s.players.size();
}
}  // namespace

Team MakeTeam(const TeamSetup& s) {
  Team team;
  team.name = s.name;
  team.players = s.players;
  team.initial_formation = s.formation;
  team.tactical_formation = s.tactical_formation;
  team.legacy_tactics = s.tactics;
  team.explicit_lineup = s.lineup.has_value();
  if (s.lineup) team.initial_lineup = *s.lineup;
  else {
    const auto count = std::min(kFormationSlots, FormationCount(s));
    Require(count <= s.players.size(), "formation has no player definition");
    for (std::size_t i = 0; i < s.players.size(); ++i) {
      if (i < count) team.initial_lineup.starters[i] = s.players[i].id;
      else team.initial_lineup.substitutes.push_back(s.players[i].id);
    }
  }
  team.leadership = s.leadership;
  team.set_piece_orders = s.set_piece_orders;
  team.formation_presets = s.formation_presets;
  ValidateTeam(team);
  return team;
}

void ValidateTeam(const Team& team) {
  std::set<PlayerId> ids;
  Require(!team.players.empty(), "team requires player definitions");
  for (const auto& p : team.players) {
    Require(p.id != kInvalidPlayerId, "invalid PlayerId");
    Require(ids.insert(p.id).second, "duplicate PlayerId");
  }
  if (team.explicit_lineup) {
    Require(team.initial_formation.size() <= kFormationSlots &&
        team.tactical_formation.size() <= kFormationSlots, "formation exceeds slot count");
  } else {
    const auto count = !team.initial_formation.empty() ? team.initial_formation.size() :
        !team.tactical_formation.empty() ? team.tactical_formation.size() : team.players.size();
    Require(count <= team.players.size(), "formation exceeds roster");
  }
  const auto member = [&](PlayerId id) { Require(ids.contains(id), "unknown team PlayerId"); };
  std::set<PlayerId> bound;
  for (const auto id : team.initial_lineup.starters) if (id) {
    member(*id);
    Require(bound.insert(*id).second, "duplicate lineup player");
  }
  for (const auto id : team.initial_lineup.substitutes) {
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
    Require(std::ranges::find(team.leadership.vice_captains, *team.leadership.captain) ==
        team.leadership.vice_captains.end(), "captain also vice captain");
  }
  const auto& o = team.set_piece_orders;
  for (const auto* list : {&o.penalties, &o.direct_free_kicks, &o.indirect_free_kicks,
                         &o.corners_left, &o.corners_right, &o.throw_ins}) order(*list);
  std::set<std::string> preset_ids;
  for (const auto& p : team.formation_presets.presets) {
    Require(!p.id.empty() && preset_ids.insert(p.id).second, "invalid or duplicate preset id");
    Require(p.kind >= FormationPresetKind::KickOff && p.kind <= FormationPresetKind::ThrowIn,
            "invalid preset kind");
    Require(p.side == FormationPresetSide::Attacking || p.side == FormationPresetSide::Defending,
            "invalid preset side");
    std::set<FormationSlotId> slots;
    for (const auto& position : p.positions) {
      Require(position.slot < kFormationSlots && slots.insert(position.slot).second,
              "invalid or duplicate preset slot");
      const auto& a = position.anchor;
      Require(std::isfinite(a.x) && std::isfinite(a.y), "nonfinite preset coordinate");
      Require(a.frame == AnchorFrame::TeamRelative || a.frame == AnchorFrame::BallRelative,
              "invalid anchor frame");
      // Ball-relative metre offsets deliberately have no artificial pitch bound:
      // candidate targets are legalized later by the planner for the actual pitch.
      if (a.frame == AnchorFrame::TeamRelative)
        Require(a.x >= 0 && a.x <= 1 && std::abs(a.y) <= 1, "normalized anchor out of range");
    }
  }
}

void ValidateMatchTeams(const std::array<Team, 2>& teams) {
  std::set<PlayerId> ids;
  for (const auto& t : teams) {
    ValidateTeam(t);
    for (const auto& p : t.players) Require(ids.insert(p.id).second, "cross-team duplicate PlayerId");
  }
}

void VacateSlot(ActiveLineupState& lineup, PlayerId player) {
  for (auto& id : lineup.slots) if (id == player) id.reset();
}
void ReplaceSlot(ActiveLineupState& lineup, FormationSlotId slot, PlayerId player) {
  Require(slot < kFormationSlots && player != kInvalidPlayerId, "invalid slot replacement");
  for (std::size_t i = 0; i < lineup.slots.size(); ++i)
    Require(i == slot || lineup.slots[i] != player, "replacement already on field");
  lineup.slots[slot] = player;
}

std::optional<PlayerId> SelectEligibleTaker(std::span<const PlayerId> priority,
    std::span<const EligiblePlayer> candidates, SelectionFallback fallback) {
  for (auto id : priority)
    for (const auto& c : candidates)
      if (c.id == id && c.eligible && c.id != kInvalidPlayerId) return id;
  if (fallback == SelectionFallback::CandidateOrder)
    for (const auto& c : candidates)
      if (c.eligible && c.id != kInvalidPlayerId) return c.id;
  return std::nullopt;
}
std::optional<PlayerId> SelectCaptain(const TeamLeadership& leadership,
    std::span<const EligiblePlayer> candidates, SelectionFallback fallback) {
  if (leadership.captain) {
    const std::array priority{*leadership.captain};
    if (auto id = SelectEligibleTaker(priority, candidates, SelectionFallback::None)) return id;
  }
  return SelectEligibleTaker(leadership.vice_captains, candidates, fallback);
}
}  // namespace football::sim
