#ifndef FOOTBALL_SIM_PLAYER_FOUL_ASSESSMENT_HPP
#define FOOTBALL_SIM_PLAYER_FOUL_ASSESSMENT_HPP

#include "football/foundation/math/vector3.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/player/player_definition.hpp"

namespace football::sim {

// The only two challenge families the rules still distinguish. A lesser fall
// (legacy trip type 1) is a harmless stumble and is never reported here.
enum class FoulKind {
  StandingFall,
  SlidingTackle,
};

// The challenge family recognised at the contact instant. Player translates
// its animation action types into this rule value, so the Referee never reads
// an animation enum to understand a challenge.
enum class ChallengeAction {
  Other,
  StandingChallenge,
  SlidingChallenge,
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
  football::player::PlayerId offender = football::player::kInvalidPlayerId;
  football::player::PlayerId victim = football::player::kInvalidPlayerId;
  int victim_team_id = -1;
  int offender_team_id = -1;
  // Normalized suspicion in [0, 1]. Not a card severity and not a rule verdict.
  float score = 0.0f;
  // Raw legacy sliding severity, frozen by the solver for the sliding kind.
  // The referee derives the card from it exactly as it did from the fact.
  float severity = 0.0f;
  Tick contacted_at{};

  // Foul location in the Stadium Frame, so a later ruling can schedule
  // a restart there. It is not the post-collision snapshot position.
  football::math::Vector3 position;
  football::math::Vector3 victim_position;
  football::math::Vector3 victim_direction;
  football::math::Vector3 offender_position;
  football::math::Vector3 ball_position;
  // The challenge family behind this contact, named as a rule value.
  ChallengeAction action = ChallengeAction::Other;
  bool offender_scheduled_contact = false;
  int offender_contact_frame = -1;
  int offender_frame = 0;
  football::math::Vector3 offender_contact_position;
  Tick offender_last_touch_tick{};
  float victim_team_fading_possession = 0.0f;
  // Solver-supplied: this foul denied an obvious goal-scoring opportunity.
  // The referee classifies the sanction from it; it is evidence, not a card.
  bool denied_goal_opportunity = false;
  // Solver-supplied: the offender was genuinely challenging for the ball (not
  // merely performing a challenge animation). Law 12 double jeopardy needs it.
  bool attempted_to_play_ball = false;
};

}  // namespace football::sim

#endif  // FOOTBALL_SIM_PLAYER_FOUL_ASSESSMENT_HPP
