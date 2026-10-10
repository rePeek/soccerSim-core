#ifndef FOOTBALL_SIM_EVENT_RULE_TOUCH_HPP
#define FOOTBALL_SIM_EVENT_RULE_TOUCH_HPP
#include "sim/event/accepted_touch.hpp"
#include "sim/player/player_body_collider_motion.hpp"
#include "football/ball/ball_contact.hpp"

namespace football::sim::event {
enum class RuleTouchSource { PreparedAction, BodyCCD, RetainAcquisition };
// Reviewed producer-side provenance, kept separately from the referee verdict.
// Physics evidence is not itself an AcceptedTouch: Simulation checks identity,
// play authorization and contact episodes before publishing through the sink.
struct RuleTouch {
  AcceptedTouch accepted;
  PlayerBodyPart part;
  RuleTouchSource source;
  std::uint64_t step = 0, generation = 0;
  std::optional<football::ball::BallContact> contact;
};
} // namespace football::sim::event
#endif
