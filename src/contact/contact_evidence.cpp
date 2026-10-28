#include "football/contact/contact_evidence.hpp"

#include <cmath>
#include "football/foundation/math/scalar.hpp"

namespace football::contact {
using namespace football::math;

std::optional<StandingFallAttribution> AttributeStandingFall(
    const StandingFallActor& first, const StandingFallActor& second) {
  if (first.team_id == second.team_id) return std::nullopt;
  constexpr float kStandingFallThreshold = 0.58f;
  const bool first_falls = first.sensitivity > kStandingFallThreshold;
  const bool second_falls = second.sensitivity > kStandingFallThreshold;
  if (!first_falls && !second_falls) return std::nullopt;

  bool victim_is_first;
  float victim_sensitivity;
  if (first_falls && second_falls) {
    if (first.sensitivity > second.sensitivity) {
      victim_is_first = true;
      victim_sensitivity = first.sensitivity;
    } else if (second.sensitivity > first.sensitivity) {
      victim_is_first = false;
      victim_sensitivity = second.sensitivity;
    } else if (first.id < second.id) {
      victim_is_first = true;
      victim_sensitivity = first.sensitivity;
    } else {
      victim_is_first = false;
      victim_sensitivity = second.sensitivity;
    }
  } else if (first_falls) {
    victim_is_first = true;
    victim_sensitivity = first.sensitivity;
  } else {
    victim_is_first = false;
    victim_sensitivity = second.sensitivity;
  }
  return StandingFallAttribution{victim_is_first,
      NormalizedClamp(victim_sensitivity, kStandingFallThreshold, 1.0f)};
}

float SlidingChallengeSeverity(const football::sim::FoulAssessment& fact) {
  float severity = 1.0;
  if (fact.offender_scheduled_contact) {
    severity = std::pow(clamp(fabs(fact.offender_contact_frame - fact.offender_frame) /
                                  fact.offender_contact_frame,
                              0.0, 1.0),
                    0.7) * 0.5;
    severity += NormalizedClamp(
        (fact.ball_position - fact.offender_contact_position).GetLength(), 0.0, 2.0) * 0.5;
  }
  severity += (fact.victim_position - fact.offender_position)
                  .GetNormalized(0)
                  .GetDotProduct(fact.victim_direction) * 0.5 + 0.5;
  return severity;
}

}  // namespace football::contact
