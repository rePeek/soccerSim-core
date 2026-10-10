#include "football/ball/pitch_colliders.hpp"

#include "foundation/math/vector3.hpp"
#include "model/ball_config.hpp"
#include "model/pitch.hpp"

namespace football::ball {
namespace {

constexpr ColliderId kGround = 1;
constexpr ColliderId kLeftPostLow = 2;
constexpr ColliderId kLeftPostHigh = 3;
constexpr ColliderId kRightPostLow = 4;
constexpr ColliderId kRightPostHigh = 5;
constexpr ColliderId kLeftBar = 6;
constexpr ColliderId kRightBar = 7;

constexpr float kPostRadius = 0.06f;

ColliderMotion StaticCollider(ColliderId id, ColliderShape shape, ContactMaterial material) {
  ColliderMotion motion;
  motion.id = id;
  motion.start = shape;
  motion.end = shape;
  motion.material = material;
  return motion;
}

}  // namespace

std::vector<ColliderMotion> BuildPitchColliders(const football::model::Pitch& pitch,
                                                const football::model::BallConfig& ball) {
  using blunted::Vector3;
  const float x = pitch.half_length();
  const float y = pitch.goal_half_width();
  const float z = pitch.goal_height();
  const ContactMaterial ground{ball.restitution(), 0.0f};
  const ContactMaterial steel{0.8f, 0.0f};

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
  return colliders;
}

}  // namespace football::ball