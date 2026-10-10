#ifndef FOOTBALL_SIM_PLAYER_FOUL_ASSESSMENT_HPP
#define FOOTBALL_SIM_PLAYER_FOUL_ASSESSMENT_HPP

#include "foundation/math/vector3.hpp"
#include "foundation/time/tick.hpp"
#include "model/player.hpp"

namespace football::sim {

// The only two challenge families the rules still distinguish. A lesser fall
// (legacy trip type 1) is a harmless stumble and is never reported here.
enum class FoulKind {
  StandingFall,
  SlidingTackle,
};

// Contact-solver foul suspicion for one effective collision. It is the only
// rule-relevant value the contact solver produces: at most one suspected
// offender per collision, with a normalized [0, 1] score. Whether that
// suspicion is actually a foul, an advantage or a card stays a Referee verdict.
//
// Every geometric/action field is frozen at the contact instant, exactly like
// the legacy PlayerTripFact evidence, so a delayed referee pass can judge from
// the contact that actually happened instead of from later actor state. The
// contact solver is the only owner that can capture them.
struct FoulAssessment {
  FoulKind kind = FoulKind::StandingFall;
  model::PlayerId offender = model::kInvalidPlayerId;
  model::PlayerId victim = model::kInvalidPlayerId;
  int victim_team_id = -1;
  int offender_team_id = -1;
  // Normalized suspicion in [0, 1]. Not a card severity and not a rule verdict.
  float score = 0.0f;
  // Raw legacy sliding severity, frozen by the solver for the sliding kind.
  // The referee derives the card from it exactly as it did from the fact.
  float severity = 0.0f;
  Tick contacted_at{};

  // Foul location in the victim's pitch frame, so a later ruling can schedule
  // a restart there. It is not the post-collision snapshot position.
  blunted::Vector3 position;
  blunted::Vector3 victim_position;
  blunted::Vector3 victim_direction;
  blunted::Vector3 offender_position;
  blunted::Vector3 ball_position;
  int offender_action_type = 0;
  bool offender_scheduled_contact = false;
  int offender_contact_frame = -1;
  int offender_frame = 0;
  blunted::Vector3 offender_contact_position;
  Tick offender_last_touch_tick{};
  float victim_team_fading_possession = 0.0f;
};

}  // namespace football::sim

#endif  // FOOTBALL_SIM_PLAYER_FOUL_ASSESSMENT_HPP
