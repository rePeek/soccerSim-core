#ifndef FOOTBALL_SIM_TOUCH_EVIDENCE_CLASSIFICATION_HPP
#define FOOTBALL_SIM_TOUCH_EVIDENCE_CLASSIFICATION_HPP

#include <cstdint>

// P6: a single value-level classification of one ball/actor evidence tuple.
// These three facts are NOT interchangeable and must never be collapsed into one
// "touch" boolean:
//   geometric contact  - shapes overlap or a sweep hits; says nothing physical
//   physical impact    - a nonzero normal impulse actually changed the ball
//   accepted rule touch- the referee-facing AcceptedTouch fact
// Overlap projection, a zero-impulse resting contact and an accepted touch are
// three different things. This header is pure so consumers can classify recorded
// evidence without re-reading live actors.
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
