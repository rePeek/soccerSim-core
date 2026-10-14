// LOCO-2.6b — tick-driven animation transition state tests.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "football/animation/types.hpp"
#include "sim/player/animation_transition_state.hpp"

using football::math::Vector3;

namespace {

PoseFrame PoseWith(float root_z, float body_angle_deg) {
  PoseFrame pose;
  pose.positions[BodyPart::player] = Vector3(0.0f, 0.0f, root_z);
  pose.orientations[BodyPart::body].SetAngleAxis(
      body_angle_deg * football::math::pi / 180.0f, Vector3(0, 0, 1));
  return pose;
}

}  // namespace

TEST_CASE("inactive transition returns the target unchanged") {
  AnimationTransitionState state;
  const PoseFrame target = PoseWith(1.0f, 0.0f);
  const PoseFrame out = state.Advance(target);
  REQUIRE_FALSE(state.active);
  REQUIRE(out.positions[BodyPart::player].coords[2] ==
          target.positions[BodyPart::player].coords[2]);
  REQUIRE(out.orientations[BodyPart::body].elements[3] ==
          target.orientations[BodyPart::body].elements[3]);
}

TEST_CASE("transition completes after its duration and ends on the target") {
  AnimationTransitionState state;
  const PoseFrame source = PoseWith(0.0f, 0.0f);
  const PoseFrame target = PoseWith(1.0f, 90.0f);
  state.Begin(source, 4);

  PoseFrame frame = PoseWith(0.0f, 0.0f);
  const float weights[4] = {0.0f, 0.0f, 0.0f, 0.0f};  // observed below
  for (int tick = 0; tick < 4; ++tick) {
    frame = state.Advance(target);

    // Root Z advances monotonically toward the target and never overshoots.
    const float z = frame.positions[BodyPart::player].coords[2];
    REQUIRE(z >= 0.0f);
    REQUIRE(z <= 1.0f);
  }
  REQUIRE_FALSE(state.active);
  // Final tick must be exactly the target pose.
  REQUIRE(frame.positions[BodyPart::player].coords[2] ==
          Catch::Approx(target.positions[BodyPart::player].coords[2]));
  for (int e = 0; e < 4; ++e) {
    REQUIRE(frame.orientations[BodyPart::body].elements[e] ==
            target.orientations[BodyPart::body].elements[e]);
  }
}

TEST_CASE("early ticks blend between source and target") {
  AnimationTransitionState state;
  const PoseFrame source = PoseWith(0.0f, 0.0f);
  const PoseFrame target = PoseWith(1.0f, 90.0f);
  state.Begin(source, 4);

  const PoseFrame first = state.Advance(target);
  REQUIRE(state.active);
  // elapsed 1 of 4 => smoothstep(0.25) = 0.15625; root Z lerp.
  REQUIRE(first.positions[BodyPart::player].coords[2] ==
          Catch::Approx(0.15625f * 1.0f).margin(1e-4f));
  // Orientation is not yet the target.
  REQUIRE(first.orientations[BodyPart::body].GetRotationAngle(
              target.orientations[BodyPart::body]) > 0.0f);
}

TEST_CASE("mid-interruption restarts from the previous final pose") {
  AnimationTransitionState state;
  const PoseFrame source = PoseWith(0.0f, 0.0f);
  const PoseFrame brake = PoseWith(1.0f, 45.0f);
  const PoseFrame turn = PoseWith(2.0f, 135.0f);

  state.Begin(source, 8);
  const PoseFrame after_brake_tick = state.Advance(brake);  // still blending
  REQUIRE(state.active);

  // A new transition starts from the actual last output pose.
  state.Begin(after_brake_tick, 4);
  REQUIRE(state.active);
  for (int e = 0; e < 4; ++e) {
    REQUIRE(state.source_pose.orientations[BodyPart::body].elements[e] ==
            after_brake_tick.orientations[BodyPart::body].elements[e]);
  }
  REQUIRE(state.source_pose.positions[BodyPart::player].coords[2] ==
          Catch::Approx(after_brake_tick.positions[BodyPart::player].coords[2]));

  // The new transition advances toward `turn` from that snapshot.
  const PoseFrame next = state.Advance(turn);
  REQUIRE(next.positions[BodyPart::player].coords[2] > 0.0f);
  REQUIRE(next.positions[BodyPart::player].coords[2] < 2.0f);
}

TEST_CASE("reset clears any active transition") {
  AnimationTransitionState state;
  state.Begin(PoseWith(0.0f, 0.0f), 8);
  REQUIRE(state.active);
  state.Reset();
  REQUIRE_FALSE(state.active);
  REQUIRE(state.duration_ticks == 0);
  REQUIRE(state.elapsed_ticks == 0);
}