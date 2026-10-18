#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "football/foundation/math/vector3.hpp"
#include "football/pitch/pitch.hpp"
#include "football/team/lineup.hpp"

namespace football::team {

enum class FormationPresetKind { KickOff, FreeKick, Corner, Penalty, ThrowIn };
enum class FormationPresetSide { Attacking, Defending };
enum class AnchorFrame { TeamRelative, BallRelative };

struct FormationAnchor {
  AnchorFrame frame = AnchorFrame::TeamRelative;
  // TeamRelative: x in [0,1] (own to opponent goal), y in [-1,1]
  // (team-right to team-left), exactly TacticalAnchor's u/v convention.
  // BallRelative: metre offsets from ball, +x attacking, +y team-left.
  float x = 0;
  float y = 0;
};

struct PresetPosition {
  FormationSlotId slot = 0;
  FormationAnchor anchor;
  // Optional descriptive task, not a runtime control or AI assignment.
  std::string responsibility;
};

struct FormationPreset {
  std::string id;
  FormationPresetKind kind = FormationPresetKind::KickOff;
  FormationPresetSide side = FormationPresetSide::Attacking;
  std::vector<PresetPosition> positions;
};

// Reusable templates owned by Team. These are never the live restart plan:
// Runtime/AI select one and derive current candidate targets from live actors.
struct FormationPresets {
  std::vector<FormationPreset> presets;
};

void ValidateFormationAnchor(const FormationAnchor& anchor);
void ValidateFormationPresets(const FormationPresets& presets);
const FormationPreset* FindFormationPreset(const FormationPresets& presets,
                                           std::string_view id);

// A planar candidate in the caller's pitch frame, NOT a legal restart placement.
// defending_direction is -1 or +1 in that SAME frame, independent of TeamSide.
// mirror_lateral swaps left/right before rotating to the pitch frame. Ball is
// read-only and only used for BallRelative; output z is always zero. No clamp,
// rule judgment, actor movement, ball placement or template mutation occurs here.
football::math::Vector3 FormationAnchorToWorld(
    const FormationAnchor& anchor, const football::pitch::Pitch& pitch,
    int defending_direction, const football::math::Vector3& ball_position,
    bool mirror_lateral = false);

}  // namespace football::team
