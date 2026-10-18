#include "football/runtime/formation_preset.hpp"

#include <cmath>
#include <set>
#include <stdexcept>

namespace football::sim {
namespace {
void Require(bool value, const char* message) {
  if (!value) throw std::invalid_argument(message);
}
}
void ValidateFormationAnchor(const FormationAnchor& a) {
  Require(std::isfinite(a.x) && std::isfinite(a.y), "nonfinite preset coordinate");
  Require(a.frame == AnchorFrame::TeamRelative || a.frame == AnchorFrame::BallRelative,
          "invalid anchor frame");
  // Metre offsets have no artificial pitch-size limit. Planner legalizes the
  // resulting candidate for the actual restart; conversion rejects overflow.
  if (a.frame == AnchorFrame::TeamRelative)
    Require(a.x >= 0 && a.x <= 1 && std::abs(a.y) <= 1, "normalized anchor out of range");
}
void ValidateFormationPresets(const FormationPresetLibrary& library) {
  std::set<std::string> ids;
  for (const auto& p : library.presets) {
    Require(!p.id.empty() && ids.insert(p.id).second, "invalid or duplicate preset id");
    Require(p.kind >= FormationPresetKind::KickOff && p.kind <= FormationPresetKind::ThrowIn,
            "invalid preset kind");
    Require(p.side == FormationPresetSide::Attacking || p.side == FormationPresetSide::Defending,
            "invalid preset side");
    std::set<FormationSlotId> slots;
    for (const auto& position : p.positions) {
      Require(position.slot < kFormationSlots && slots.insert(position.slot).second,
              "invalid or duplicate preset slot");
      ValidateFormationAnchor(position.anchor);
    }
  }
}
const FormationPreset* FindFormationPreset(const FormationPresetLibrary& library,
                                           std::string_view id) {
  for (const auto& preset : library.presets) if (preset.id == id) return &preset;
  return nullptr;
}
football::math::Vector3 FormationAnchorToWorld(
    const FormationAnchor& a, const football::pitch::Pitch& pitch,
    int defending_direction, const football::math::Vector3& ball_position,
    bool mirror_lateral) {
  ValidateFormationAnchor(a);
  Require(defending_direction == -1 || defending_direction == 1, "invalid defending direction");
  const float attack_sign = -static_cast<float>(defending_direction);
  const float lateral = mirror_lateral ? -a.y : a.y;
  football::math::Vector3 result;
  if (a.frame == AnchorFrame::TeamRelative) {
    // Exactly TacticalAnchor u/v: x=0 own goal, x=1 opponent goal.
    result = {attack_sign * (a.x - 0.5f) * pitch.length(),
              attack_sign * lateral * pitch.half_width(), 0};
  } else {
    Require(std::isfinite(ball_position.coords[0]) && std::isfinite(ball_position.coords[1]),
            "nonfinite ball anchor origin");
    result = {ball_position.coords[0] + attack_sign * a.x,
              ball_position.coords[1] + attack_sign * lateral, 0};
  }
  Require(std::isfinite(result.coords[0]) && std::isfinite(result.coords[1]),
          "formation candidate overflow");
  return result;
}
}  // namespace football::sim
