#ifndef FOOTBALL_SIM_TOUCH_EVIDENCE_CLASSIFICATION_HPP
#define FOOTBALL_SIM_TOUCH_EVIDENCE_CLASSIFICATION_HPP

#include <cstdint>
#include "football/foundation/math/vector3.hpp"

namespace football::sim {
enum class TouchEvidence {
  None = 0,
  GeometricOverlapOnly,   // geometry present, no impulse, no rule fact
  PhysicalImpactOnly,     // nonzero impulse, no rule fact (not yet attributed)
  AcceptedTouchOnly,      // rule fact without a recorded physical cause
  PhysicalImpactAccepted, // nonzero impulse that is the rule touch
};

struct TouchEvidenceInput {
  bool geometric_contact = false;
  bool physical_impact = false;   // normal impulse > 0
  bool accepted_rule_touch = false;
};

inline TouchEvidence ClassifyTouchEvidence(const TouchEvidenceInput& input) {
  if (input.accepted_rule_touch)
    return input.physical_impact ? TouchEvidence::PhysicalImpactAccepted
                                 : TouchEvidence::AcceptedTouchOnly;
  if (input.physical_impact) return TouchEvidence::PhysicalImpactOnly;
  if (input.geometric_contact) return TouchEvidence::GeometricOverlapOnly;
  return TouchEvidence::None;
}

// P6c: a boot contact is a deliberate kick only while the boot is actively
// swinging relative to the player root. A planted support foot carries the
// root velocity (near-zero swing) and is never promoted to a deliberate
// strike just because the action is a Shot/Pass.
inline constexpr float kMinDeliberateKickSwingSpeed = 4.0f; // m/s
inline bool IsDeliberateKickSwing(const blunted::Vector3& surface_velocity,
                                  const blunted::Vector3& player_velocity) {
  return (surface_velocity - player_velocity).GetLength() >
         kMinDeliberateKickSwingSpeed;
}

// Evidence that must still be recorded authoritatively because a Snapshot alone
// cannot disambiguate it: an accepted rule touch is always an authority fact,
// and a physical impact with no rule fact is an unexplained authority gap that
// must stay visible until P5c-3 attribution covers it.
inline bool NeedsAuthoritativeRecord(TouchEvidence evidence) {
  return evidence == TouchEvidence::AcceptedTouchOnly ||
         evidence == TouchEvidence::PhysicalImpactAccepted ||
         evidence == TouchEvidence::PhysicalImpactOnly;
}

}  // namespace football::sim

#endif
