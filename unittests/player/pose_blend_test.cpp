// LOCO-2.6a — pure pose blend kernel tests.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "football/animation/pose_blend.hpp"
#include "football/animation/types.hpp"

using football::math::Vector3;

namespace {

PoseFrame IdentityPose(float root_z) {
  PoseFrame pose;
  pose.positions[BodyPart::player] = Vector3(0.0f, 0.0f, root_z);
  return pose;
}

}  // namespace

TEST_CASE("weights 0 and 1 reproduce source and target exactly") {
  const PoseFrame source = IdentityPose(0.0f);
  PoseFrame target = IdentityPose(0.5f);
  target.orientations[BodyPart::body].SetAngleAxis(
      90.0f * football::math::pi / 180.0f, Vector3(0, 0, 1));

  const PoseFrame at_source = BlendPose(source, target, 0.0f);
  const PoseFrame at_target = BlendPose(source, target, 1.0f);

  for (std::size_t b = 0; b < kBodyPartCount; ++b) {
    for (int e = 0; e < 4; ++e) {
      REQUIRE(at_source.orientations[b].elements[e] ==
              source.orientations[b].elements[e]);
      REQUIRE(at_target.orientations[b].elements[e] ==
              target.orientations[b].elements[e]);
    }
    for (int c = 0; c < 3; ++c) {
      REQUIRE(at_source.positions[b].coords[c] == source.positions[b].coords[c]);
      REQUIRE(at_target.positions[b].coords[c] == target.positions[b].coords[c]);
    }
  }
}

TEST_CASE("midpoint slerps orientation and lerps root Z") {
  const PoseFrame source = IdentityPose(0.0f);
  PoseFrame target = IdentityPose(0.5f);
  target.orientations[BodyPart::body].SetAngleAxis(
      90.0f * football::math::pi / 180.0f, Vector3(0, 0, 1));

  const PoseFrame mid = BlendPose(source, target, 0.5f);
  REQUIRE(mid.positions[BodyPart::player].coords[2] == Catch::Approx(0.25f));

  const float to_source =
      mid.orientations[BodyPart::body].GetRotationAngle(
          source.orientations[BodyPart::body]);
  const float to_target =
      mid.orientations[BodyPart::body].GetRotationAngle(
          target.orientations[BodyPart::body]);
  REQUIRE(to_source == Catch::Approx(football::math::pi / 4.0f).margin(1e-3f));
  REQUIRE(to_target == Catch::Approx(football::math::pi / 4.0f).margin(1e-3f));
}

TEST_CASE("weights outside [0,1] clamp to the nearest endpoint") {
  const PoseFrame source = IdentityPose(0.0f);
  const PoseFrame target = IdentityPose(1.0f);

  const PoseFrame low = BlendPose(source, target, -0.7f);
  const PoseFrame high = BlendPose(source, target, 3.0f);
  REQUIRE(low.positions[BodyPart::player].coords[2] ==
          Catch::Approx(source.positions[BodyPart::player].coords[2]));
  REQUIRE(high.positions[BodyPart::player].coords[2] ==
          Catch::Approx(target.positions[BodyPart::player].coords[2]));
}

TEST_CASE("blending is deterministic and finite") {
  const PoseFrame source = IdentityPose(0.0f);
  PoseFrame target = IdentityPose(0.8f);
  target.orientations[BodyPart::body].SetAngleAxis(
      120.0f * football::math::pi / 180.0f, Vector3(0, 0, 1));

  for (const float w : {0.1f, 0.25f, 0.5f, 0.75f, 0.9f}) {
    const PoseFrame a = BlendPose(source, target, w);
    const PoseFrame b = BlendPose(source, target, w);
    for (std::size_t j = 0; j < kBodyPartCount; ++j) {
      for (int e = 0; e < 4; ++e) {
        REQUIRE(a.orientations[j].elements[e] == b.orientations[j].elements[e]);
        REQUIRE(std::isfinite(a.orientations[j].elements[e]));
      }
      for (int c = 0; c < 3; ++c) {
        REQUIRE(a.positions[j].coords[c] == b.positions[j].coords[c]);
        REQUIRE(std::isfinite(a.positions[j].coords[c]));
      }
    }
  }
}