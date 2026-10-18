#pragma once

#include <string>
#include <vector>
#include "football/runtime/team_lineup.hpp"

namespace football::sim {
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
struct FormationPresetLibrary {
  std::vector<FormationPreset> presets;
};
}  // namespace football::sim
