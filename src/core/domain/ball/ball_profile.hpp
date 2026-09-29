#ifndef _HPP_CORE_DOMAIN_BALL_PROFILE
#define _HPP_CORE_DOMAIN_BALL_PROFILE

namespace football::domain {

// Match-lifetime properties of the ball. Contact material and environment
// parameters are separate; they are not intrinsic properties of the ball.
struct BallProfile {
  float radius = 0.11f;  // meters
  float mass = 0.43f;    // kilograms
  // Moment of inertia as I = inertiaFactor * mass * radius^2. A thin
  // spherical shell is 2/3, a solid sphere is 2/5. Kept as a profile datum
  // so future calibration never needs to touch the resolver math.
  float inertiaFactor = 2.0f / 3.0f;
};

}  // namespace football::domain

#endif  // _HPP_CORE_DOMAIN_BALL_PROFILE
