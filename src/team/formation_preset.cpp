#include "football/team/formation_preset.hpp"

#include <array>
#include <cmath>
#include <set>
#include <stdexcept>

namespace football::team {
namespace {
void Require(bool value, const char* message) {
  if (!value) throw std::invalid_argument(message);
}
}  // namespace

void ValidateFormationAnchor(const FormationAnchor& anchor) {
  Require(std::isfinite(anchor.x) && std::isfinite(anchor.y),
          "nonfinite preset coordinate");
  Require(anchor.frame == AnchorFrame::TeamRelative ||
              anchor.frame == AnchorFrame::BallRelative,
          "invalid anchor frame");
  // Metre offsets have no artificial pitch-size limit. The planner legalizes
  // the resulting candidate for the actual restart; conversion rejects overflow.
  if (anchor.frame == AnchorFrame::TeamRelative) {
    Require(anchor.x >= 0 && anchor.x <= 1 && std::abs(anchor.y) <= 1,
            "normalized anchor out of range");
  }
}

void ValidateFormationPresets(const FormationPresets& presets) {
  std::set<std::string> ids;
  for (const auto& preset : presets.presets) {
    Require(!preset.id.empty() && ids.insert(preset.id).second,
            "invalid or duplicate preset id");
    Require(preset.kind >= FormationPresetKind::KickOff &&
                preset.kind <= FormationPresetKind::ThrowIn,
            "invalid preset kind");
    Require(preset.side == FormationPresetSide::Attacking ||
                preset.side == FormationPresetSide::Defending,
            "invalid preset side");
    std::set<FormationSlotId> slots;
    for (const auto& position : preset.positions) {
      Require(position.slot < kFormationSlots &&
                  slots.insert(position.slot).second,
              "invalid or duplicate preset slot");
      ValidateFormationAnchor(position.anchor);
    }
  }
}

const FormationPreset* FindFormationPreset(const FormationPresets& presets,
                                           std::string_view id) {
  for (const auto& preset : presets.presets) {
    if (preset.id == id) return &preset;
  }
  return nullptr;
}

football::math::Vector3 FormationAnchorToWorld(
    const FormationAnchor& anchor, const football::pitch::Pitch& pitch,
    int defending_direction, const football::math::Vector3& ball_position,
    bool mirror_lateral) {
  ValidateFormationAnchor(anchor);
  Require(defending_direction == -1 || defending_direction == 1,
          "invalid defending direction");
  const float attack_sign = -static_cast<float>(defending_direction);
  const float lateral = mirror_lateral ? -anchor.y : anchor.y;
  football::math::Vector3 result;
  if (anchor.frame == AnchorFrame::TeamRelative) {
    // Exactly TacticalAnchor u/v: x=0 own goal, x=1 opponent goal.
    result = {attack_sign * (anchor.x - 0.5f) * pitch.length(),
              attack_sign * lateral * pitch.half_width(), 0};
  } else {
    Require(std::isfinite(ball_position.coords[0]) &&
                std::isfinite(ball_position.coords[1]),
            "nonfinite ball anchor origin");
    result = {ball_position.coords[0] + attack_sign * anchor.x,
              ball_position.coords[1] + attack_sign * lateral, 0};
  }
  Require(std::isfinite(result.coords[0]) &&
              std::isfinite(result.coords[1]),
          "formation candidate overflow");
  return result;
}

}  // namespace football::team
