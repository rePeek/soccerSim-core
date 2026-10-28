#ifndef FOOTBALL_CONTACT_CONTACT_EVIDENCE_HPP
#define FOOTBALL_CONTACT_CONTACT_EVIDENCE_HPP

#include <optional>
#include "football/contact/foul_assessment.hpp"

namespace football::contact {

struct StandingFallActor {
  football::player::PlayerId id;
  int team_id;
  float sensitivity;
};
struct StandingFallAttribution {
  bool victim_is_first;
  float score;
};

// Contact evidence only: same-team/harmless falls produce no suspicion. Rules
// still own the foul/card decision. Equal falls use the stable PlayerId order.
std::optional<StandingFallAttribution> AttributeStandingFall(
    const StandingFallActor& first, const StandingFallActor& second);
float SlidingChallengeSeverity(const football::sim::FoulAssessment& fact);

}  // namespace football::contact
#endif
