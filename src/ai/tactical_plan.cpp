#include "football/ai/tactical_plan.hpp"

#include <algorithm>
#include <cstddef>

namespace football::ai {
namespace {
using football::sim::TeamSide;

struct Point {
  float x = 0.0f;
  float y = 0.0f;
};

// Normalized role anchor in the interpreter's (-1..1) shape space: x is depth
// (negative = own goal), y is lateral (positive = own left). Mirrors the
// historical MakeTacticalBoard RoleAnchor so the bootstrap describes the same
// declared shape.
Point RolePoint(e_PlayerRole role) {
  switch (role) {
    case e_PlayerRole_GK:
    case e_PlayerRole_CB: return {-1.0f, 0.0f};
    case e_PlayerRole_LB: return {-0.8f, 0.8f};
    case e_PlayerRole_RB: return {-0.8f, -0.8f};
    case e_PlayerRole_DM: return {-0.5f, 0.0f};
    case e_PlayerRole_LM: return {0.0f, 1.0f};
    case e_PlayerRole_RM: return {0.0f, -1.0f};
    case e_PlayerRole_AM: return {0.5f, 0.0f};
    case e_PlayerRole_CF: return {1.0f, 0.0f};
    default: return {0.0f, 0.0f};
  }
}

PossessionRole MapPossessionRole(e_PlayerRole role) {
  switch (role) {
    case e_PlayerRole_GK: return PossessionRole::Goalkeeper;
    case e_PlayerRole_CB: return PossessionRole::CentreBack;
    case e_PlayerRole_LB:
    case e_PlayerRole_RB: return PossessionRole::FullBack;
    case e_PlayerRole_DM: return PossessionRole::HoldingMidfielder;
    case e_PlayerRole_CM: return PossessionRole::BoxToBox;
    case e_PlayerRole_LM:
    case e_PlayerRole_RM: return PossessionRole::Winger;
    case e_PlayerRole_AM: return PossessionRole::Playmaker;
    case e_PlayerRole_CF: return PossessionRole::CentreForward;
  }
  return PossessionRole::CentreBack;
}

DefendingRole MapDefendingRole(e_PlayerRole role) {
  switch (role) {
    case e_PlayerRole_GK: return DefendingRole::Goalkeeper;
    case e_PlayerRole_CB: return DefendingRole::CoveringDefender;
    case e_PlayerRole_LB:
    case e_PlayerRole_RB: return DefendingRole::MarkingDefender;
    case e_PlayerRole_DM:
    case e_PlayerRole_CM: return DefendingRole::ScreeningMidfielder;
    case e_PlayerRole_LM:
    case e_PlayerRole_RM: return DefendingRole::TrackingWinger;
    case e_PlayerRole_AM: return DefendingRole::PressingMidfielder;
    case e_PlayerRole_CF: return DefendingRole::PressingForward;
  }
  return DefendingRole::CoveringDefender;
}

e_PlayerRole DefaultRoleAt(std::size_t index) {
  switch (index) {
    case 0: return e_PlayerRole_GK;
    case 1:
    case 2:
    case 3: return e_PlayerRole_CB;
    case 4: return e_PlayerRole_DM;
    case 5:
    case 6: return e_PlayerRole_CM;
    case 7:
    case 8: return e_PlayerRole_LM;
    default: return e_PlayerRole_CF;
  }
}

TacticalAnchor AnchorFromPoint(Point point) {
  point.x = std::clamp(point.x, -1.0f, 1.0f);
  point.y = std::clamp(point.y, -1.0f, 1.0f);
  TacticalAnchor anchor;
  anchor.u = std::clamp((point.x + 1.0f) * 0.5f, 0.0f, 1.0f);
  anchor.v = point.y;
  return anchor;
}
}  // namespace

TacticalAnchor PossessionRoleAnchor(PossessionRole role) {
  switch (role) {
    case PossessionRole::Goalkeeper: return {0.03f, 0.0f};
    case PossessionRole::CentreBack: return {0.2f, 0.0f};
    case PossessionRole::FullBack: return {0.28f, 0.7f};
    case PossessionRole::InvertedFullBack: return {0.34f, 0.28f};
    case PossessionRole::HoldingMidfielder: return {0.4f, 0.0f};
    case PossessionRole::Playmaker: return {0.62f, 0.0f};
    case PossessionRole::BoxToBox: return {0.55f, 0.0f};
    case PossessionRole::Winger: return {0.72f, 0.85f};
    case PossessionRole::InsideForward: return {0.8f, 0.45f};
    case PossessionRole::CentreForward: return {0.88f, 0.0f};
    case PossessionRole::FalseNine: return {0.72f, 0.0f};
  }
  return {0.5f, 0.0f};
}

TacticalAnchor DefendingRoleAnchor(DefendingRole role) {
  switch (role) {
    case DefendingRole::Goalkeeper: return {0.03f, 0.0f};
    case DefendingRole::CoveringDefender: return {0.18f, 0.0f};
    case DefendingRole::MarkingDefender: return {0.28f, 0.65f};
    case DefendingRole::ScreeningMidfielder: return {0.42f, 0.0f};
    case DefendingRole::PressingMidfielder: return {0.55f, 0.0f};
    case DefendingRole::TrackingWinger: return {0.45f, 0.8f};
    case DefendingRole::PressingForward: return {0.66f, 0.0f};
    case DefendingRole::HoldingForward: return {0.58f, 0.0f};
  }
  return {0.5f, 0.0f};
}

TacticalPlan MakeTacticalPlan(const football::sim::TeamSetup& team,
                                  football::sim::TeamSide side,
                                  const football::pitch::Pitch& pitch) {
  (void)side;
  (void)pitch;
  TacticalPlan plan;
  plan.mentality = Mentality::Balanced;
  plan.in_possession = DefaultInPossession(Mentality::Balanced);
  plan.out_of_possession = DefaultOutOfPossession(Mentality::Balanced);

  for (std::size_t i = 0; i < plan.slots.size(); ++i) {
    TacticalSlot& slot = plan.slots[i];
    slot.id = static_cast<TacticalSlotId>(i);

    e_PlayerRole role = DefaultRoleAt(i);
    Point point = RolePoint(role);
    if (i < team.tactical_formation.size()) {
      const auto& entry = team.tactical_formation[i];
      role = entry.role;
      const Point anchor = RolePoint(role);
      point.x = 0.6f * entry.position.x + 0.4f * anchor.x;
      point.y = 0.6f * entry.position.y + 0.4f * anchor.y;
    } else if (i < team.formation.size()) {
      const auto& entry = team.formation[i];
      role = entry.role;
      const Point anchor = RolePoint(role);
      // Legacy public initial-position lateral convention.
      point.x = 0.6f * entry.position.x + 0.4f * anchor.x;
      point.y = 0.6f * (entry.position.y * -2.36f) + 0.4f * anchor.y;
    }
    // Explicit initial formation overrides roles, matching the legacy shape.
    if (i < team.formation.size()) role = team.formation[i].role;

    slot.in_possession = AnchorFromPoint(point);
    slot.out_of_possession = slot.in_possession;
    slot.in_possession_role = MapPossessionRole(role);
    slot.out_of_possession_role = MapDefendingRole(role);
  }
  return plan;
}

}  // namespace football::ai
