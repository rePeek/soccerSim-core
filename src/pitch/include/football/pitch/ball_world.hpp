#ifndef FOOTBALL_PITCH_BALL_WORLD_HPP
#define FOOTBALL_PITCH_BALL_WORLD_HPP

// Pitch -> BallWorld assembly: the only place that knows both Pitch and Ball's
// static environment. Header-only (inline): the runtime and the offline baker
// share one definition, and no module toolchain or extra CMake is needed.
//
// Consumers that only need the pitch description include football/pitch/pitch.hpp
// instead; this header is what makes football::pitch depend on football::ball.

#include <vector>

#include "football/ball/ball_model.hpp"
#include "football/ball/ball_world.hpp"
#include "football/ball/collider.hpp"
#include "football/pitch/pitch.hpp"
#include "foundation/math/vector3.hpp"

namespace football::pitch {
namespace detail {

using football::ball::Capsule;
using football::ball::ColliderId;
using football::ball::ColliderMotion;
using football::ball::ColliderShape;
using football::ball::ContactMaterial;
using football::ball::Plane;

// Stable ids (also the deterministic tie-break order); geometry and order are
// frozen because the CCD uses ColliderId for deterministic contact sorting.
inline constexpr ColliderId kGround = 1;
inline constexpr ColliderId kLeftPostLow = 2;
inline constexpr ColliderId kLeftPostHigh = 3;
inline constexpr ColliderId kRightPostLow = 4;
inline constexpr ColliderId kRightPostHigh = 5;
inline constexpr ColliderId kLeftBar = 6;
inline constexpr ColliderId kRightBar = 7;

inline constexpr float kPostRadius = 0.06f;

inline ColliderMotion StaticCollider(ColliderId id, ColliderShape shape,
                                     ContactMaterial material) {
  ColliderMotion motion;
  motion.id = id;
  motion.start = shape;
  motion.end = shape;
  motion.material = material;
  return motion;
}

}  // namespace detail

// Assembles the complete static BallWorld from the pitch description and the
// active ball material. Deterministic; constructs once per match.
inline football::ball::BallWorld BuildBallWorld(
    const Pitch& pitch, const football::ball::BallModel& ball) {
  using namespace detail;
  using blunted::Vector3;
  const float x = pitch.half_length();
  const float y = pitch.goal_half_width();
  const float z = pitch.goal_height();
  const ContactMaterial ground{ball.restitution(), 0.0f};
  const ContactMaterial steel{0.8f, 0.0f};

  football::ball::BallWorld world;
  world.ground = {pitch.ground_deceleration(), pitch.quadratic_resistance(),
                  pitch.grass_height()};
  world.net = {pitch.half_length(), pitch.goal_half_width(), pitch.goal_height(),
               pitch.goal_depth()};

  std::vector<ColliderMotion> colliders;
  colliders.reserve(7);
  colliders.push_back(StaticCollider(kGround, Plane{Vector3(0, 0, 0), Vector3(0, 0, 1)}, ground));
  colliders.push_back(StaticCollider(kLeftPostLow,
      Capsule{Vector3(-x, -y, 0), Vector3(-x, -y, z), kPostRadius}, steel));
  colliders.push_back(StaticCollider(kLeftPostHigh,
      Capsule{Vector3(-x, y, 0), Vector3(-x, y, z), kPostRadius}, steel));
  colliders.push_back(StaticCollider(kRightPostLow,
      Capsule{Vector3(x, -y, 0), Vector3(x, -y, z), kPostRadius}, steel));
  colliders.push_back(StaticCollider(kRightPostHigh,
      Capsule{Vector3(x, y, 0), Vector3(x, y, z), kPostRadius}, steel));
  colliders.push_back(StaticCollider(kLeftBar,
      Capsule{Vector3(-x, -y, z), Vector3(-x, y, z), kPostRadius}, steel));
  colliders.push_back(StaticCollider(kRightBar,
      Capsule{Vector3(x, -y, z), Vector3(x, y, z), kPostRadius}, steel));
  world.static_colliders = std::move(colliders);
  return world;
}

}  // namespace football::pitch

#endif  // FOOTBALL_PITCH_BALL_WORLD_HPP
