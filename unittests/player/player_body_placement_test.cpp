// BODY-1.8 headless body placement validation.
//
// Constructs nine standing-pose players (PoseFrame{} = identity bind rotations,
// Player Root at z = 0) on a real Pitch(105, 68) whose ground HalfSpace is at
// z = 0, and checks the world-space body geometry: joint proportions, left/right
// symmetry, collider size/placement and the support-foot height. No Simulation,
// no Humanoid, no animation asset, no RNG and no full match.
//
// The reference pose is a *standing* pose, not a running one: it is the same
// zero-rotation bind pose used by BODY-1.4, now placed in pitch coordinates so
// the foot-to-ground relationship becomes meaningful. Sampling a real Idle
// animation frame is deliberately deferred to the stage-2 animation work,
// where idle-clip selection is defined.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <variant>

#include "football/animation/clip.hpp"
#include "football/foundation/geometry/shapes.hpp"
#include "football/pitch/pitch.hpp"
#include "player/player_baked_body_pose.hpp"
#include "player/player_baked_contact_regions.hpp"
#include "player/player_body_profile.hpp"
#include "player/player_kinematics.hpp"

using football::geometry::Capsule;
using football::geometry::ColliderShape;
using football::geometry::HalfSpace;
using football::geometry::Sphere;
using football::math::Vector3;
using football::player::MakePlayerBodyProfile;
using football::player::PlayerBodyProfile;
using football::player::PlayerDefinition;

namespace {

struct ProfileCase {
  float height;
  float mass;
};

const std::array<ProfileCase, 9> kNineProfiles{{
    {1.65f, 53.0f}, {1.65f, 63.0f}, {1.65f, 78.0f},
    {1.80f, 64.0f}, {1.80f, 75.0f}, {1.80f, 90.0f},
    {1.95f, 75.0f}, {1.95f, 88.0f}, {1.95f, 105.0f},
}};

// Distinct placements inside the 105x68 pitch.
Vector3 Placement(std::size_t index) {
  const float xs[9] = {-30.0f, 0.0f, 30.0f, -30.0f, 0.0f, 30.0f, -30.0f, 0.0f, 30.0f};
  const float ys[9] = {-20.0f, -20.0f, -20.0f, 0.0f, 0.0f, 0.0f, 20.0f, 20.0f, 20.0f};
  return Vector3(xs[index], ys[index], 0.0f);
}

PlayerBodyProfile MakeProfile(float height, float mass) {
  PlayerDefinition definition;
  definition.height = height;
  definition.weight_kg = mass;
  return MakePlayerBodyProfile(definition);
}

const Capsule& AsCapsule(const ColliderShape& shape) { return std::get<Capsule>(shape); }
const Sphere& AsSphere(const ColliderShape& shape) { return std::get<Sphere>(shape); }

struct Aabb {
  Vector3 min{1e9f, 1e9f, 1e9f};
  Vector3 max{-1e9f, -1e9f, -1e9f};

  void Extend(const Vector3& v) {
    for (int i = 0; i < 3; ++i) {
      min.coords[i] = std::min(min.coords[i], v.coords[i]);
      max.coords[i] = std::max(max.coords[i], v.coords[i]);
    }
  }
  void ExtendShape(const ColliderShape& shape) {
    if (std::holds_alternative<Capsule>(shape)) {
      const auto& c = AsCapsule(shape);
      for (int i = 0; i < 3; ++i) {
        min.coords[i] = std::min({min.coords[i], c.a.coords[i] - c.radius, c.b.coords[i] - c.radius});
        max.coords[i] = std::max({max.coords[i], c.a.coords[i] + c.radius, c.b.coords[i] + c.radius});
      }
    } else if (std::holds_alternative<Sphere>(shape)) {
      const auto& s = AsSphere(shape);
      for (int i = 0; i < 3; ++i) {
        min.coords[i] = std::min(min.coords[i], s.center.coords[i] - s.radius);
        max.coords[i] = std::max(max.coords[i], s.center.coords[i] + s.radius);
      }
    }
  }
};

float CapsuleMinZ(const Capsule& capsule) {
  return std::min(capsule.a.coords[2], capsule.b.coords[2]) - capsule.radius;
}

struct PlacementSample {
  BakedBodyPose pose;
  BakedPlayerBodyCollider body;
  std::array<ColliderShape, kBakedContactRegionCount> regions;
  Aabb aabb;
  float left_boot_min_z;
  float right_boot_min_z;
};

PlacementSample Measure(const PoseFrame& frame, const PlayerKinematicState& state,
                        float base_angle, const PlayerBodyProfile& profile) {
  PlacementSample out;
  out.pose = EvaluateBakedBodyPose(frame, state, base_angle, profile);
  out.body = BuildBodyCollider(frame, state, base_angle, profile);
  out.regions = BuildBakedContactRegions(frame, state, base_angle, profile);
  for (const auto& shape : out.body.shapes) out.aabb.ExtendShape(shape);
  for (const auto& shape : out.regions) out.aabb.ExtendShape(shape);
  out.left_boot_min_z = CapsuleMinZ(AsCapsule(out.regions[1]));
  out.right_boot_min_z = CapsuleMinZ(AsCapsule(out.regions[2]));
  return out;
}

// Optional, bounded text/CSV diagnostic for later animation stages. Enabled
// only via FOOTBALL_BODY_PLACEMENT_DIAG; it records Player Root, every joint,
// capsule summary and the foot-to-ground distances.
void WritePlacementDiagnostics() {
  if (std::getenv("FOOTBALL_BODY_PLACEMENT_DIAG") == nullptr) return;
  const PoseFrame bind{};
  std::fprintf(stderr,
               "player,height,weight,root_x,root_y,root_z,hip_z,knee_z,ankle_z,"
               "boot_min_z,l_boot_min,r_boot_min,body_height,body_width\n");
  for (std::size_t i = 0; i < kNineProfiles.size(); ++i) {
    const auto profile = MakeProfile(kNineProfiles[i].height, kNineProfiles[i].mass);
    PlayerKinematicState state;
    state.position = Placement(i);
    const auto s = Measure(bind, state, 0.0f, profile);
    const float boot_min = std::min(s.left_boot_min_z, s.right_boot_min_z);
    const float body_height = s.aabb.max.coords[2] - s.aabb.min.coords[2];
    const float body_width = s.aabb.max.coords[0] - s.aabb.min.coords[0];
    std::fprintf(stderr, "P%02zu,%.2f,%.1f,%.2f,%.2f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n",
                 i + 1, profile.height_m, profile.mass_kg,
                 s.pose.joints[player].coords[0], s.pose.joints[player].coords[1],
                 s.pose.joints[player].coords[2],
                 s.pose.joints[body].coords[2], s.pose.joints[left_knee].coords[2],
                 s.pose.joints[left_ankle].coords[2], boot_min,
                 s.left_boot_min_z, s.right_boot_min_z, body_height, body_width);
    std::fprintf(stderr, "  joints:");
    for (std::size_t j = 0; j < kBodyPartCount; ++j) {
      std::fprintf(stderr, " %zu=(%.3f,%.3f,%.3f)", j,
                   s.pose.joints[j].coords[0], s.pose.joints[j].coords[1],
                   s.pose.joints[j].coords[2]);
    }
    std::fprintf(stderr, "\n");
  }
}

}  // namespace

TEST_CASE("pitch ground is at z = 0 and placements stay inside") {
  const football::pitch::Pitch pitch(105.0f, 68.0f);
  REQUIRE(pitch.length() == 105.0f);
  REQUIRE(pitch.width() == 68.0f);
  REQUIRE(pitch.grass_height() == Catch::Approx(0.025f));

  REQUIRE_FALSE(pitch.colliders().empty());
  const auto& ground = pitch.colliders()[0].start;
  REQUIRE(std::holds_alternative<HalfSpace>(ground));
  const auto& half_space = std::get<HalfSpace>(ground);
  REQUIRE(half_space.point.coords[2] == Catch::Approx(0.0f));
  REQUIRE(half_space.normal.coords[2] == Catch::Approx(1.0f));

  for (std::size_t i = 0; i < kNineProfiles.size(); ++i) {
    REQUIRE(pitch.contains(Placement(i).coords[0], Placement(i).coords[1]));
  }
}

TEST_CASE("standing pose writes pitch-space geometry diagnostics") {
  WritePlacementDiagnostics();
  SUCCEED();
}

TEST_CASE("standing pose joints are identical across weights at equal height") {
  const PoseFrame bind{};
  for (const float height : {1.65f, 1.80f, 1.95f}) {
    std::optional<BakedBodyPose> previous;
    for (const ProfileCase& c : kNineProfiles) {
      if (c.height != height) continue;
      PlayerKinematicState state;
      state.position = Vector3(2.0f, -3.0f, 0.0f);
      const auto world = EvaluateBakedBodyPose(bind, state, 0.0f, MakeProfile(c.height, c.mass));
      if (previous) {
        for (std::size_t j = 0; j < kBodyPartCount; ++j)
          for (int k = 0; k < 3; ++k)
            REQUIRE(world.joints[j].coords[k] == previous->joints[j].coords[k]);
      }
      previous = world;
    }
  }
}

TEST_CASE("standing pose joint proportions scale with height") {
  const PoseFrame bind{};
  PlayerKinematicState state;
  const auto sample = [&](float height) {
    const auto world = EvaluateBakedBodyPose(bind, state, 0.0f, MakeProfile(height, 23.0f * height * height));
    return world.joints;
  };

  const auto j165 = sample(1.65f);
  const auto j180 = sample(1.80f);
  const auto j195 = sample(1.95f);
  const float hs180 = MakeProfile(1.80f, 1.0f).height_scale;
  const float hs195 = MakeProfile(1.95f, 1.0f).height_scale;

  // Hip, knee, ankle and neck heights are proportional to height_scale.
  REQUIRE(j180[body].coords[2] == Catch::Approx(0.96f * hs180).margin(1e-3f));
  REQUIRE(j195[body].coords[2] == Catch::Approx(0.96f * hs195).margin(1e-3f));
  REQUIRE(j180[left_knee].coords[2] == Catch::Approx(0.53f * hs180).margin(1e-3f));
  REQUIRE(j195[left_knee].coords[2] == Catch::Approx(0.53f * hs195).margin(1e-3f));
  REQUIRE(j180[left_ankle].coords[2] == Catch::Approx(0.09f * hs180).margin(1e-3f));
  REQUIRE(j195[neck].coords[2] == Catch::Approx(1.61f * hs195).margin(1e-3f));

  // Same mass across heights may land in different build buckets, but the bone
  // separations still scale with height_scale alone.
  const float sep165 = j165[body].coords[2] - j165[left_ankle].coords[2];
  const float sep195 = j195[body].coords[2] - j195[left_ankle].coords[2];
  const float ratio = hs195 / hs180;  // 1.95/1.80
  REQUIRE(sep195 == Catch::Approx(sep165 * (hs195 / MakeProfile(1.65f, 1.0f).height_scale)).margin(1e-3f));
}

TEST_CASE("standing pose is left-right symmetric for every profile") {
  const PoseFrame bind{};
  PlayerKinematicState state;
  state.position = Vector3(4.0f, 5.0f, 0.0f);
  for (const ProfileCase& c : kNineProfiles) {
    const auto world = EvaluateBakedBodyPose(bind, state, 0.0f, MakeProfile(c.height, c.mass));
    const float root_x = world.joints[player].coords[0];
    for (const auto [l, r] : {std::pair{left_thigh, right_thigh}, {left_knee, right_knee},
                              {left_ankle, right_ankle}, {left_shoulder, right_shoulder},
                              {left_elbow, right_elbow}}) {
      REQUIRE(world.joints[l].coords[0] == Catch::Approx(2.0f * root_x - world.joints[r].coords[0]).margin(1e-4f));
      REQUIRE(world.joints[l].coords[1] == Catch::Approx(world.joints[r].coords[1]).margin(1e-4f));
      REQUIRE(world.joints[l].coords[2] == Catch::Approx(world.joints[r].coords[2]).margin(1e-4f));
    }
  }
}

TEST_CASE("nine standing players are separated with sane body extents") {
  const PoseFrame bind{};
  std::array<PlacementSample, 9> samples;
  for (std::size_t i = 0; i < kNineProfiles.size(); ++i) {
    PlayerKinematicState state;
    state.position = Placement(i);
    samples[i] = Measure(bind, state, 0.0f, MakeProfile(kNineProfiles[i].height, kNineProfiles[i].mass));
  }

  for (std::size_t a = 0; a < samples.size(); ++a)
    for (std::size_t b = a + 1; b < samples.size(); ++b) {
      const float dx = Placement(a).coords[0] - Placement(b).coords[0];
      const float dy = Placement(a).coords[1] - Placement(b).coords[1];
      REQUIRE(std::sqrt(dx * dx + dy * dy) > 1.0f);
    }

  // Taller nominal heights have taller body envelopes.
  float previous_height = 0.0f;
  for (std::size_t i = 0; i < samples.size(); ++i) {
    const float h = samples[i].aabb.max.coords[2] - samples[i].aabb.min.coords[2];
    const float w = samples[i].aabb.max.coords[0] - samples[i].aabb.min.coords[0];
    REQUIRE(h > 1.2f);
    REQUIRE(h < 2.2f);
    REQUIRE(w > 0.2f);
    REQUIRE(w < 0.9f);
    REQUIRE(std::isfinite(h));
    REQUIRE(std::isfinite(w));
    if (i > 0) REQUIRE(h >= previous_height - 0.01f);  // non-decreasing with height groups
    previous_height = h;
  }
}

TEST_CASE("support foot is near the ground while the ankle stays above it") {
  const PoseFrame bind{};
  for (std::size_t i = 0; i < kNineProfiles.size(); ++i) {
    PlayerKinematicState state;
    state.position = Placement(i);
    const auto profile = MakeProfile(kNineProfiles[i].height, kNineProfiles[i].mass);
    const auto s = Measure(bind, state, 0.0f, profile);

    // Player Root sits on the pitch plane.
    REQUIRE(s.pose.joints[player].coords[2] == Catch::Approx(0.0f));

    // Ankle joints are a skeletal quantity and must stay clearly above zero.
    const float ankle = std::min(s.pose.joints[left_ankle].coords[2],
                                 s.pose.joints[right_ankle].coords[2]);
    REQUIRE(ankle > 0.05f);

    // The support-foot boot volume pokes only slightly below the ground plane
    // (the reference boot capsule bottom is -0.038 * height_scale); it is
    // ball-foot contact geometry, not a foot-ground solver, so it is "near
    // z = 0" rather than exactly zero.
    const float support = std::min(s.left_boot_min_z, s.right_boot_min_z);
    CAPTURE(profile.height_m, profile.mass_kg, ankle, support);
    REQUIRE(support > -0.06f);
    REQUIRE(support < 0.02f);

    // Head top ≈ 1.9032 * height_scale: `height` is the nominal body height,
    // not the collision envelope top (documented, not silently re-tuned).
    REQUIRE(s.aabb.max.coords[2] == Catch::Approx(1.9032f * profile.height_scale).margin(0.01f));
  }
}