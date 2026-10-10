#ifndef FOOTBALL_SIM_PLAYER_FOUL_ASSESSMENT_HPP
#define FOOTBALL_SIM_PLAYER_FOUL_ASSESSMENT_HPP

#include "foundation/math/vector3.hpp"
#include "model/player.hpp"

namespace football::sim {

// Contact-solver foul suspicion for one effective collision. It is the only
// rule-relevant value the contact solver produces: at most one suspected
// offender per collision, with a normalized [0, 1] score. Whether that
// suspicion is actually a foul, an advantage or a card stays a Referee verdict.
//
// `position` is the contact location in the victim's pitch frame, frozen at the
// contact instant, so a later ruling can schedule a restart there. It is not
// the post-collision snapshot position.
struct FoulAssessment {
  model::PlayerId offender = model::kInvalidPlayerId;
  model::PlayerId victim = model::kInvalidPlayerId;
  float score = 0.0f;
  blunted::Vector3 position;
};

}  // namespace football::sim

#endif  // FOOTBALL_SIM_PLAYER_FOUL_ASSESSMENT_HPP
