#ifndef FOOTBALL_CORE_PHYSICS_CONTACT_BALL_GOAL_GEOMETRY_HPP
#define FOOTBALL_CORE_PHYSICS_CONTACT_BALL_GOAL_GEOMETRY_HPP

namespace football_sim::contact {

// Goal / pitch geometry consumed by woodwork contact detection.
struct GoalGeometry {
  float halfWidth = 55.0f;
  float goalHalfWidth = 3.7f;
  float goalHeight = 2.5f;
  float postRadius = 0.07f;
  float postAbsorbInv = 0.8f;
};

}  // namespace football_sim::contact

#endif  // FOOTBALL_CORE_PHYSICS_CONTACT_BALL_GOAL_GEOMETRY_HPP
