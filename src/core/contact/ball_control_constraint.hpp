#ifndef _HPP_CORE_CONTACT_BALL_CONTROL_CONSTRAINT
#define _HPP_CORE_CONTACT_BALL_CONTROL_CONSTRAINT

namespace football::contact {

// 7G-10: foundation for short-lived ball-control authority. Tactical/technical
// intents (trap, catch, carry) need more than a single impact: the ball stays
// coupled to its owner for a short time. Stage 8 intents will emit one of
// these instead of mutating BallState directly.
//
// Deliberately minimal: no Match, no PlayerBase, no lifetime bookkeeping. The
// world/physics layer decides when the constraint activates and expires.
enum class BallControlType {
  Trap,   // ground control, ball held within foot reach
  Catch,  // keeper holds the ball
  Carry,  // ball carried while moving (dribble)
};

struct BallControlConstraint {
  int ownerId = -1;  // stable player id; -1 means no owner
  BallControlType type = BallControlType::Trap;

  bool IsActive() const { return ownerId >= 0; }
};

}  // namespace football::contact

#endif  // _HPP_CORE_CONTACT_BALL_CONTROL_CONSTRAINT