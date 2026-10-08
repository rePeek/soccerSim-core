#ifndef FOOTBALL_BALL_BALL_CONFIG_HPP
#define FOOTBALL_BALL_BALL_CONFIG_HPP

namespace football::ball {

// Immutable physical parameters of the ball.
//
// The defaults reproduce the legacy empirical kernel bit-for-bit. Do not
// replace them with textbook values during the module-cohesion phase: physics
// upgrades (Magnus, rolling, continuous collision detection, contact-point
// impulse/angular coupling) are intentionally deferred until the module is
// stable and carry their own differential verification.
struct BallConfig {
  float mass = 0.43f;
  float radius = 0.11f;

  float restitution = 0.62f;
  float friction = 0.04f;
  float drag = 0.015f;
};

}  // namespace football::ball

#endif  // FOOTBALL_BALL_BALL_CONFIG_HPP
