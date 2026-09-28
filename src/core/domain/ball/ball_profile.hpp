#ifndef _HPP_CORE_DOMAIN_BALL_PROFILE
#define _HPP_CORE_DOMAIN_BALL_PROFILE

namespace football::domain {

// Match-lifetime properties of the ball. Contact material and environment
// parameters are separate; they are not intrinsic properties of the ball.
struct BallProfile {
  float radius = 0.11f;  // meters
  float mass = 0.43f;    // kilograms; contact dynamics will consume this later
};

}  // namespace football::domain

#endif  // _HPP_CORE_DOMAIN_BALL_PROFILE
