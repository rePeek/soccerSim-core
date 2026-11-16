#pragma once

// football::ball owns the ball end to end: its state, the impulse response, the
// one-tick contact resolution and the Ball facade. Contact detection itself
// lives in football::collision (generic swept queries); the material response
// and the tick rules live here, next to the state they act on.

#include <span>
#include <vector>

#include "football/foundation/geometry/collider.hpp"
#include "football/foundation/math/quaternion.hpp"
#include "football/foundation/math/vector3.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/models.h"

namespace football::pitch { class Pitch; }

namespace football::ball {

using namespace football::pitch;

struct BallState {
  math::Vector3 position;
  math::Vector3 velocity;
  math::Vector3 angular_velocity;
  math::Quaternion orientation;
};

struct BallContact {
  geometry::ColliderId collider = 0;
  float toi = 0.0f;
  // World point where the ball surface met the collider surface: ball centre -
  // normal * radius. The impulse acts here, so the offset from the centre is
  // what turns a straight hit into spin.
  math::Vector3 point;
  math::Vector3 normal;
  math::Vector3 relative_velocity;
  float normal_impulse = 0.0f;
  bool position_corrected = false;
  math::Vector3 surface_velocity = math::Vector3(0);
};

struct BallStepResult {
  BallState state;
  // At most one entry carries a non-zero normal impulse: a tick resolves one
  // impact and integrates the rest of itself collision-free. Any further
  // entries are zero-impulse overlap repairs recorded before that impact.
  std::vector<BallContact> contacts;
};

// ---- Impulse response ----
// Thin-shell sphere inertia factor, I = kBallInertiaFactor * mass * radius^2.
inline constexpr float kBallInertiaFactor = 2.0f / 3.0f;

float BallInertia(const BallModel& config);

// The only way a ball state changes: an impulse [N*s] applied at a world point.
// Gameplay (pass, shot) supplies the impulse itself; contact resolution derives
// one from the material in SolveContactImpulse. Position is deliberately left
// untouched -- placing the ball at a contact belongs to the contact layer.
BallState ApplyImpulseAtPoint(const BallState& ball,
                              const math::Vector3& impulse,
                              const math::Vector3& world_point,
                              const BallModel& config);

struct ContactImpulse {
  // normal * jn + jt, ready for ApplyImpulseAtPoint at contact.point.
  math::Vector3 impulse{0};
  // Zero when the contact is already separating: nothing to apply, and callers
  // use it to tell a real impact from an overlap repair.
  float normal_impulse = 0.0f;
};

// Impulse for one contact: restitution sets the normal part, Coulomb friction
// caps the tangential part by it. `contact.point` is the lever arm, recorded by
// the contact layer as `ball position - normal * radius`.
ContactImpulse SolveContactImpulse(const BallState& ball,
                                   const BallContact& contact,
                                   const geometry::ContactMaterial& material,
                                   const BallModel& config);

// ---- One tick of ball physics ----
// Advances one tick against the scene, resolving at most one impact.
BallStepResult AdvanceBallTick(const BallState& initial,
                               std::span<const geometry::ColliderMotion> colliders,
                               float dt,
                               const BallModel& config,
                               const Pitch& pitch);

class Ball {
 public:
  explicit Ball(const BallModel& model =
      ::football_ball_model{0.43f, 0.11f, 0.02f});

  BallState get_state() const { return state_; }
  void set_state(const BallState& state) { state_ = state; }
  const BallModel& model() const { return model_; }

  // The pitch is the scene a tick resolves against, so it is a per-call
  // argument: Ball owns no reference to it and outlives no owner.
  BallStepResult Step(const Pitch& pitch);
  BallStepResult Predict(const Pitch& pitch) const;
  BallState Predict(const Pitch& pitch,
                    time::TickSpan ahead) const;

  // Authoritative ground placement: z is forced to the ball radius and
  // velocity/spin are cleared. Airborne states still use set_state(BallState).
  void PlaceOnGround(float x, float y);

 private:
  BallStepResult EvaluateTick(const Pitch& pitch) const;

  BallModel model_;
  BallState state_;
};

}  // namespace football::ball
