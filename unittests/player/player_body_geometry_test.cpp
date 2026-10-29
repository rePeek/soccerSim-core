// Player body geometry: the same 14-node default skeleton must express nine
// height/build combinations purely from PlayerBodyProfile. This file tests the
// static bind pose only -- no Simulation, no Humanoid, no RNG, no animation
// library. It links football::collision/animation (geometry + foundation) and
// the private src/ tree, never football::sim.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdlib>
#include <set>
#include <variant>

#include "football/animation/clip.hpp"
#include "football/foundation/collision/sweep.hpp"
#include "football/foundation/geometry/collider.hpp"
#include "football/foundation/geometry/shapes.hpp"
#include "player/execution/player_baked_body_pose.hpp"
#include "player/execution/player_baked_contact_regions.hpp"
#include "player/execution/player_baked_pose_sample.hpp"
#include "football/player/player_body_profile.hpp"
#include "football/player/player_kinematics.hpp"

using football::geometry::Capsule;
using football::geometry::ColliderId;
using football::geometry::ColliderShape;
using football::geometry::Sphere;
using football::math::Vector3;
using football::player::BodyBuild;
using football::player::MakePlayerBodyProfile;
using football::player::kReferenceBmi;
using football::player::PlayerBodyProfile;
using football::player::PlayerDefinition;

namespace {

PlayerBodyProfile MakeProfile(float height, float mass) {
  PlayerDefinition definition;
  definition.height = height;
  definition.weight_kg = mass;
  return MakePlayerBodyProfile(definition);
}

// Identity orientations and zero keyframe positions: the standing bind pose.
PoseFrame StandingPose() { return PoseFrame{}; }

const Capsule& AsCapsule(const ColliderShape& shape) {
  return std::get<Capsule>(shape);
}

const Sphere& AsSphere(const ColliderShape& shape) {
  return std::get<Sphere>(shape);
}

float CapsuleLength(const Capsule& capsule) {
  const float dx = capsule.b.coords[0] - capsule.a.coords[0];
  const float dy = capsule.b.coords[1] - capsule.a.coords[1];
  const float dz = capsule.b.coords[2] - capsule.a.coords[2];
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

Vector3 MirrorVec(const Vector3& v) {
  return Vector3(-v.coords[0], -v.coords[1], v.coords[2]);
}

bool VecClose(const Vector3& a, const Vector3& b) {
  for (int i = 0; i < 3; ++i) {
    if (!(a.coords[i] == Catch::Approx(b.coords[i]).margin(1e-4f))) return false;
  }
  return true;
}

// Optional, bounded diagnostic dump. Enabled only via
// FOOTBALL_BODY_GEOMETRY_DIAG; it writes a fixed number of lines per profile
// and never depends on Simulation, Godot or rendering.
void DumpBodyGeometryDiagnostics() {
  if (std::getenv("FOOTBALL_BODY_GEOMETRY_DIAG") == nullptr) return;
  const struct {
    float height;
    float mass;
  } cases[] = {{1.65f, 53.0f}, {1.65f, 63.0f}, {1.65f, 78.0f},
               {1.80f, 64.0f}, {1.80f, 75.0f}, {1.80f, 90.0f},
               {1.95f, 75.0f}, {1.95f, 88.0f}, {1.95f, 105.0f}};
  const PoseFrame pose = StandingPose();
  const PlayerKinematicState state{};
  for (const auto& c : cases) {
    const auto profile = MakeProfile(c.height, c.mass);
    const auto world = EvaluateBakedBodyPose(pose, state, 0.0f, profile);
    const auto segments = BuildBodyCollider(pose, state, 0.0f, profile);
    std::fprintf(stderr,
                 "profile h=%.2f m=%.1f build=%d hs=%.4f gs=%.4f | hip=%.3f "
                 "knee=%.3f ankle=%.3f neck=%.3f | torso_r=%.3f head_r=%.3f "
                 "thigh_r=%.3f shin_r=%.3f\n",
                 profile.height_m, profile.mass_kg, static_cast<int>(profile.build),
                 profile.height_scale, profile.girth_scale,
                 world.joints[body].coords[2], world.joints[left_knee].coords[2],
                 world.joints[left_ankle].coords[2], world.joints[neck].coords[2],
                 AsCapsule(segments.shapes[0]).radius,
                 AsSphere(segments.shapes[1]).radius,
                 AsCapsule(segments.shapes[2]).radius,
                 AsCapsule(segments.shapes[3]).radius);
  }
}

}  // namespace

TEST_CASE("same height yields identical joints regardless of weight") {
  const PoseFrame pose = StandingPose();
  const PlayerKinematicState state{};
  const auto slim = MakeProfile(1.80f, 64.0f);
  const auto normal = MakeProfile(1.80f, 75.0f);
  const auto broad = MakeProfile(1.80f, 90.0f);

  const auto js = EvaluateBakedBodyPose(pose, state, 0.0f, slim);
  const auto jn = EvaluateBakedBodyPose(pose, state, 0.0f, normal);
  const auto jb = EvaluateBakedBodyPose(pose, state, 0.0f, broad);

  for (std::size_t i = 0; i < kBodyPartCount; ++i) {
    for (int c = 0; c < 3; ++c) {
      REQUIRE(js.joints[i].coords[c] == jn.joints[i].coords[c]);
      REQUIRE(jn.joints[i].coords[c] == jb.joints[i].coords[c]);
    }
  }
}

TEST_CASE("joint heights scale with height and root XY stays un-scaled") {
  const PoseFrame pose = StandingPose();
  PlayerKinematicState state;
  state.position = Vector3(3.0f, -2.0f, 0.0f);
  const auto profile = MakeProfile(1.80f, 75.0f);
  const auto world = EvaluateBakedBodyPose(pose, state, 0.0f, profile);

  // Root XY is Player-owned and must not be re-scaled by the height ratio.
  REQUIRE(world.joints[player].coords[0] == Catch::Approx(3.0f));
  REQUIRE(world.joints[player].coords[1] == Catch::Approx(-2.0f));
  REQUIRE(world.joints[player].coords[2] == Catch::Approx(0.0f));

  const float hs = profile.height_scale;
  REQUIRE(world.joints[body].coords[2] == Catch::Approx(0.96f * hs).margin(1e-4f));
  REQUIRE(world.joints[middle].coords[2] == Catch::Approx(1.11f * hs).margin(1e-4f));
  REQUIRE(world.joints[neck].coords[2] == Catch::Approx(1.61f * hs).margin(1e-4f));
  REQUIRE(world.joints[left_knee].coords[2] == Catch::Approx(0.53f * hs).margin(1e-4f));
  REQUIRE(world.joints[left_ankle].coords[2] == Catch::Approx(0.09f * hs).margin(1e-4f));
  REQUIRE(world.joints[left_shoulder].coords[2] == Catch::Approx(1.59f * hs).margin(1e-4f));

  // A taller player is taller everywhere.
  const auto tall = MakeProfile(1.95f, 88.0f);
  const auto world_tall = EvaluateBakedBodyPose(pose, state, 0.0f, tall);
  REQUIRE(world_tall.joints[neck].coords[2] > world.joints[neck].coords[2]);
}

TEST_CASE("left and right joints stay symmetric") {
  const PoseFrame pose = StandingPose();
  const PlayerKinematicState state{};
  const auto profile = MakeProfile(1.80f, 75.0f);
  const auto world = EvaluateBakedBodyPose(pose, state, 0.0f, profile);

  const std::array<std::pair<BodyPart, BodyPart>, 5> pairs{{
      {left_thigh, right_thigh},
      {left_knee, right_knee},
      {left_ankle, right_ankle},
      {left_shoulder, right_shoulder},
      {left_elbow, right_elbow},
  }};
  for (const auto& [l, r] : pairs) {
    REQUIRE(world.joints[l].coords[0] == Catch::Approx(-world.joints[r].coords[0]).margin(1e-5f));
    REQUIRE(world.joints[l].coords[1] == Catch::Approx(world.joints[r].coords[1]).margin(1e-5f));
    REQUIRE(world.joints[l].coords[2] == Catch::Approx(world.joints[r].coords[2]).margin(1e-5f));
  }
}

TEST_CASE("all joints and orientations are finite") {
  const PoseFrame pose = StandingPose();
  const PlayerKinematicState state{};
  const auto world = EvaluateBakedBodyPose(pose, state, 0.0f, MakeProfile(1.80f, 90.0f));
  for (std::size_t i = 0; i < kBodyPartCount; ++i) {
    for (int c = 0; c < 3; ++c) {
      REQUIRE(std::isfinite(world.joints[i].coords[c]));
    }
    for (int e = 0; e < 4; ++e) {
      REQUIRE(std::isfinite(world.orientations[i].elements[e]));
    }
  }
}

TEST_CASE("broad > normal > slim radii at equal height; bones do not lengthen") {
  const PoseFrame pose = StandingPose();
  const PlayerKinematicState state{};
  const auto slim = MakeProfile(1.80f, 64.0f);
  const auto normal = MakeProfile(1.80f, 75.0f);
  const auto broad = MakeProfile(1.80f, 90.0f);

  const auto seg_s = BuildBodyCollider(pose, state, 0.0f, slim);
  const auto seg_n = BuildBodyCollider(pose, state, 0.0f, normal);
  const auto seg_b = BuildBodyCollider(pose, state, 0.0f, broad);

  // Torso (pelvis) radius orders with build; girth factor is 1.0 there.
  REQUIRE(AsCapsule(seg_s.shapes[0]).radius < AsCapsule(seg_n.shapes[0]).radius);
  REQUIRE(AsCapsule(seg_n.shapes[0]).radius < AsCapsule(seg_b.shapes[0]).radius);

  // Thigh/shin girth is moderate but still ordered.
  REQUIRE(AsCapsule(seg_s.shapes[2]).radius < AsCapsule(seg_n.shapes[2]).radius);
  REQUIRE(AsCapsule(seg_n.shapes[2]).radius < AsCapsule(seg_b.shapes[2]).radius);

  // Bone lengths (endpoint distance) are a height property, not a weight one.
  for (const std::size_t segment : {2u, 3u, 4u, 5u}) {
    REQUIRE(CapsuleLength(AsCapsule(seg_s.shapes[segment])) ==
            Catch::Approx(CapsuleLength(AsCapsule(seg_n.shapes[segment]))).margin(1e-5f));
    REQUIRE(CapsuleLength(AsCapsule(seg_n.shapes[segment])) ==
            Catch::Approx(CapsuleLength(AsCapsule(seg_b.shapes[segment]))).margin(1e-5f));
  }

  // Head and boots do not bloat with weight (girth influence is zero).
  REQUIRE(AsSphere(seg_s.shapes[1]).radius == AsSphere(seg_b.shapes[1]).radius);
  const auto region_s = BuildBakedContactRegions(pose, state, 0.0f, slim);
  const auto region_b = BuildBakedContactRegions(pose, state, 0.0f, broad);
  REQUIRE(AsCapsule(region_s[1]).radius == AsCapsule(region_b[1]).radius);
  REQUIRE(AsCapsule(region_s[2]).radius == AsCapsule(region_b[2]).radius);
}

TEST_CASE("leg capsule endpoints coincide with their joints") {
  const PoseFrame pose = StandingPose();
  const PlayerKinematicState state{};
  const auto profile = MakeProfile(1.80f, 75.0f);
  const auto world = EvaluateBakedBodyPose(pose, state, 0.0f, profile);
  const auto segments = BuildBodyCollider(pose, state, 0.0f, profile);

  REQUIRE(VecClose(AsCapsule(segments.shapes[2]).a, world.joints[left_thigh]));
  REQUIRE(VecClose(AsCapsule(segments.shapes[2]).b, world.joints[left_knee]));
  REQUIRE(VecClose(AsCapsule(segments.shapes[3]).a, world.joints[left_knee]));
  REQUIRE(VecClose(AsCapsule(segments.shapes[3]).b, world.joints[left_ankle]));
  REQUIRE(VecClose(AsCapsule(segments.shapes[4]).a, world.joints[right_thigh]));
  REQUIRE(VecClose(AsCapsule(segments.shapes[4]).b, world.joints[right_knee]));
  REQUIRE(VecClose(AsCapsule(segments.shapes[5]).a, world.joints[right_knee]));
  REQUIRE(VecClose(AsCapsule(segments.shapes[5]).b, world.joints[right_ankle]));

  for (std::size_t i = 0; i < segments.shapes.size(); ++i) {
    if (std::holds_alternative<Capsule>(segments.shapes[i])) {
      REQUIRE(AsCapsule(segments.shapes[i]).radius > 0.0f);
    } else {
      REQUIRE(AsSphere(segments.shapes[i]).radius > 0.0f);
    }
  }
}

TEST_CASE("radii match the documented per-part girth formula") {
  const PoseFrame pose = StandingPose();
  const PlayerKinematicState state{};
  const auto profile = MakeProfile(1.80f, 90.0f);  // Broad
  const auto segments = BuildBodyCollider(pose, state, 0.0f, profile);
  const auto regions = BuildBakedContactRegions(pose, state, 0.0f, profile);

  const float hs = profile.height_scale;
  const float gs = profile.girth_scale;

  REQUIRE(AsCapsule(segments.shapes[0]).radius ==
          Catch::Approx(.20f * hs * BakedGirthScale(gs, kBakedGirthTorso)).margin(1e-5f));
  REQUIRE(AsSphere(segments.shapes[1]).radius ==
          Catch::Approx(kBakedHeadRadius * hs * BakedGirthScale(gs, kBakedGirthHead)).margin(1e-5f));
  REQUIRE(AsCapsule(segments.shapes[2]).radius ==
          Catch::Approx(kBakedThighRadius * hs * BakedGirthScale(gs, kBakedGirthThigh)).margin(1e-5f));
  REQUIRE(AsCapsule(segments.shapes[3]).radius ==
          Catch::Approx(kBakedShinRadius * hs * BakedGirthScale(gs, kBakedGirthShin)).margin(1e-5f));

  // Chest is the torso region; arms follow the upper-arm/forearm factors.
  REQUIRE(AsCapsule(regions[0]).radius ==
          Catch::Approx(.233f * hs * BakedGirthScale(gs, kBakedGirthTorso)).margin(1e-5f));
  REQUIRE(AsCapsule(regions[3]).radius ==
          Catch::Approx(.072f * hs * BakedGirthScale(gs, kBakedGirthUpperArm)).margin(1e-5f));
  REQUIRE(AsCapsule(regions[5]).radius ==
          Catch::Approx(.053f * hs * BakedGirthScale(gs, kBakedGirthForearm)).margin(1e-5f));
}

TEST_CASE("standing body AABB height is sane and scales with height") {
  const PoseFrame pose = StandingPose();
  const PlayerKinematicState state{};
  for (const float height : {1.65f, 1.80f, 1.95f}) {
    const auto profile = MakeProfile(height, 75.0f);
    const auto segments = BuildBodyCollider(pose, state, 0.0f, profile);
    const auto regions = BuildBakedContactRegions(pose, state, 0.0f, profile);

    float min_z = 1e9f, max_z = -1e9f;
    const auto extend = [&](const Vector3& center, float radius) {
      min_z = std::min(min_z, center.coords[2] - radius);
      max_z = std::max(max_z, center.coords[2] + radius);
    };
    for (const auto& shape : segments.shapes) {
      if (std::holds_alternative<Capsule>(shape)) {
        const auto& c = AsCapsule(shape);
        extend(c.a, c.radius);
        extend(c.b, c.radius);
      } else {
        const auto& s = AsSphere(shape);
        extend(s.center, s.radius);
      }
    }
    for (const auto& shape : regions) {
      if (std::holds_alternative<Capsule>(shape)) {
        const auto& c = AsCapsule(shape);
        extend(c.a, c.radius);
        extend(c.b, c.radius);
      } else {
        const auto& s = AsSphere(shape);
        extend(s.center, s.radius);
      }
    }

    // Head top ≈ 1.9032 * height_scale; boot bottom ≈ -0.038 * height_scale.
    REQUIRE(max_z == Catch::Approx(1.9032f * profile.height_scale).margin(0.01f));
    REQUIRE(min_z == Catch::Approx(-0.038f * profile.height_scale).margin(0.005f));
    REQUIRE(min_z < 0.0f);  // boots reach below the root reference.
  }
}

TEST_CASE("collider ids are stable and stay inside the reserved bands") {
  std::set<ColliderId> segment_ids;
  for (int i = 0; i < static_cast<int>(kBakedBodySegmentCount); ++i) {
    const auto id = BakedBodyColliderId(3, static_cast<BakedBodySegment>(i));
    segment_ids.insert(id);
    REQUIRE(id >= kFirstBakedBodyColliderId);
  }
  REQUIRE(segment_ids.size() == kBakedBodySegmentCount);

  std::set<ColliderId> region_ids;
  for (int i = 0; i < static_cast<int>(kBakedContactRegionCount); ++i) {
    const auto id = BakedContactRegionId(3, static_cast<BakedContactRegion>(i));
    region_ids.insert(id);
    REQUIRE(id >= kFirstBakedContactRegionId);
    REQUIRE(id < kFirstDynamicBodyColliderId);
  }
  REQUIRE(region_ids.size() == kBakedContactRegionCount);
}


TEST_CASE("body geometry is consumable by Overlap and Sweep") {
  const PoseFrame pose = StandingPose();
  const PlayerKinematicState state{};
  const auto profile = MakeProfile(1.80f, 75.0f);
  const auto segments = BuildBodyCollider(pose, state, 0.0f, profile);

  const auto& head = AsSphere(segments.shapes[1]);
  const Sphere ball_on_head{head.center, 0.11f};
  REQUIRE(football::collision::Overlap(ball_on_head, head));
  const Sphere far_ball{Vector3(100.0f, 100.0f, 100.0f), 0.11f};
  REQUIRE_FALSE(football::collision::Overlap(far_ball, head));

  // A ball dropped from above the head must find a swept contact over one tick.
  const auto motions = BuildBakedBodyColliderMotions(segments, segments, 0);
  const football::collision::MovingSphere falling{
      Sphere{Vector3(0.0f, 0.0f, 3.0f), 0.11f}, Vector3(0.0f, 0.0f, -2.0f)};
  const auto hit = football::collision::FirstHit(falling, motions, 1.0f);
  REQUIRE(hit.has_value());
  REQUIRE(hit->toi >= 0.0f);
  REQUIRE(hit->toi <= 1.0f);
  REQUIRE(std::isfinite(hit->center.coords[2]));
}

TEST_CASE("body-profile girth changes ball CCD consistently") {
  const PoseFrame pose = StandingPose();
  const PlayerKinematicState state{};
  const float height = 1.80f;

  const auto slim = MakeProfile(height, 64.0f);
  const auto normal = MakeProfile(height, 75.0f);
  const auto broad = MakeProfile(height, 90.0f);
  const auto segs = [](const football::player::PlayerBodyProfile& p) {
    return BuildBodyCollider(StandingPose(), PlayerKinematicState{}, 0.0f, p);
  };
  const auto motions = [](const auto& body) {
    return BuildBakedBodyColliderMotions(body, body, 0);
  };

  // Same ball, same algorithm: only the body radius differs, so the wider
  // torso is reached sooner along a horizontal sweep from the left.
  const football::collision::MovingSphere ball{
      Sphere{Vector3(-0.5f, 0.0f, 1.0f), 0.11f}, Vector3(1.0f, 0.0f, 0.0f)};

  const auto hit_slim = football::collision::FirstHit(ball, motions(segs(slim)), 1.0f);
  const auto hit_normal = football::collision::FirstHit(ball, motions(segs(normal)), 1.0f);
  const auto hit_broad = football::collision::FirstHit(ball, motions(segs(broad)), 1.0f);
  REQUIRE(hit_slim.has_value());
  REQUIRE(hit_normal.has_value());
  REQUIRE(hit_broad.has_value());

  REQUIRE(hit_broad->toi < hit_normal->toi);
  REQUIRE(hit_normal->toi < hit_slim->toi);

  const auto torso_id = BakedBodyColliderId(0, BakedBodySegment::Torso);
  REQUIRE(hit_slim->collider == torso_id);
  REQUIRE(hit_normal->collider == torso_id);
  REQUIRE(hit_broad->collider == torso_id);
}

TEST_CASE("a definition without a mass keeps reference girth geometry") {
  const PoseFrame pose = StandingPose();
  const PlayerKinematicState state{};
  const float height = 1.80f;

  PlayerDefinition no_weight;
  no_weight.height = height;  // weight_kg stays unset
  const auto no_mass = MakePlayerBodyProfile(no_weight);

  PlayerDefinition standard;
  standard.height = height;
  standard.weight_kg = kReferenceBmi * height * height;  // girth_scale == 1
  const auto standard_profile = MakePlayerBodyProfile(standard);

  REQUIRE(no_mass.girth_scale == Catch::Approx(1.0f));
  REQUIRE(standard_profile.girth_scale == Catch::Approx(1.0f));

  const auto a = BuildBodyCollider(pose, state, 0.0f, no_mass);
  const auto b = BuildBodyCollider(pose, state, 0.0f, standard_profile);
  for (std::size_t i = 0; i < a.shapes.size(); ++i) {
    if (std::holds_alternative<Capsule>(a.shapes[i])) {
      REQUIRE(CapsuleLength(AsCapsule(a.shapes[i])) ==
              Catch::Approx(CapsuleLength(AsCapsule(b.shapes[i]))).margin(1e-6f));
      REQUIRE(AsCapsule(a.shapes[i]).radius == AsCapsule(b.shapes[i]).radius);
    } else {
      REQUIRE(AsSphere(a.shapes[i]).radius == AsSphere(b.shapes[i]).radius);
    }
  }
}

TEST_CASE("optional bounded geometry diagnostics") {
  // No-op unless FOOTBALL_BODY_GEOMETRY_DIAG is set; bounded to the nine cases.
  DumpBodyGeometryDiagnostics();
  SUCCEED();
}
