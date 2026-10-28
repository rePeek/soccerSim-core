// ANIM-1 A6.4 A8.0: value-only tests for the extracted reference-motion kernel
// and the pure movement math it uses. No actors, RNG, assets or Simulation.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "football/animation/movement_math.hpp"
#include "football/animation/movement_selection.hpp"
#include "player/command/player_motion.hpp"

using Catch::Approx;
using football::animation::CalculateMovementAtFrame;
using football::animation::StretchSprintTo;

namespace {

AnimationClip MakeClip(e_DefString type, uint32_t frames, float per_frame_y,
                       Vector3 outgoing_movement, float outgoing_velocity,
                       float outgoing_angle, int touch_frame = -1,
                       int trip_type = 0) {
  AnimationClip clip;
  clip.frame_count = frames;
  clip.root_positions.reserve(frames);
  for (uint32_t i = 0; i < frames; ++i) {
    clip.root_positions.push_back(Vector3(0, per_frame_y * i, 0));
  }
  clip.metadata.action_type = static_cast<int32_t>(type);
  clip.metadata.outgoing_movement = outgoing_movement;
  clip.metadata.outgoing_velocity = outgoing_velocity;
  clip.metadata.outgoing_angle = outgoing_angle;
  clip.metadata.touch_frame = touch_frame;
  clip.metadata.trip_type = trip_type;
  return clip;
}

ActionMotionInput BaseInput() {
  ActionMotionInput input;
  input.spatial_movement = Vector3(0);
  input.angle = 0.0f;
  input.direction_vec = Vector3(0, -1, 0);
  input.float_velocity = 0.0f;
  input.use_desired_movement = false;
  input.desired_movement = Vector3(0);
  input.use_desired_body_direction = false;
  input.desired_body_direction_rel = Vector3(0, -1, 0);
  input.parameters.max_velocity = 8.0f;
  return input;
}

}  // namespace

TEST_CASE("movement math: CalculateMovementAtFrame boundary and smoothing",
          "[player][motion][a8.0]") {
  const std::vector<Vector3> positions = {Vector3(0, 0, 0), Vector3(0, -0.05f, 0),
                                          Vector3(0, -0.10f, 0), Vector3(0, -0.15f, 0)};
  // Frame 0 uses 0 -> 1, the last frame uses last-1 -> last (both unsmoothed).
  CHECK(CalculateMovementAtFrame(positions, 0).coords[1] == Approx(-5.0f));
  CHECK(CalculateMovementAtFrame(positions, 3).coords[1] == Approx(-5.0f));
  // The middle frame averages the window.
  const Vector3 middle = CalculateMovementAtFrame(positions, 2, 1);
  CHECK(middle.coords[1] == Approx(-5.0f));
}

TEST_CASE("movement math: StretchSprintTo keeps the walk part",
          "[player][motion][a8.0]") {
  // Below the walk/sprint switch the velocity is unchanged.
  CHECK(StretchSprintTo(5.0f, 8.0f, 9.0f) == 5.0f);
  // Sprint part is stretched proportionally: 7.0 -> 7.0 + (1.0)*(9-6)/(8-6).
  CHECK(StretchSprintTo(7.0f, 8.0f, 9.0f) == Approx(7.5f));
}

TEST_CASE("A8.0: a zero-motion, zero-spatial Shot plans nothing",
          "[player][motion][a8.0]") {
  const AnimationClip clip =
      MakeClip(e_DefString_Shot, 10, 0.0f, Vector3(0), 0.0f, 0.0f);
  ActionMotionInput input = BaseInput();
  const ActionMotionPlan plan = PlayerMotion::BuildActionMotionPlan(clip, input);
  REQUIRE(plan.positions.size() == 10u);
  for (const Vector3& p : plan.positions) {
    CHECK(p.coords[0] == 0.0f);
    CHECK(p.coords[1] == 0.0f);
  }
  CHECK(plan.outgoing_velocity == Vector3(0));
  CHECK(plan.rotation_offset == 0.0f);
}

TEST_CASE("A8.0: a forward Movement clip generates a forward trajectory",
          "[player][motion][a8.0]") {
  const Vector3 outgoing(0, -5.0f, 0);
  const AnimationClip clip =
      MakeClip(e_DefString_Movement, 20, -0.05f, outgoing, 5.0f, 0.0f);
  ActionMotionInput input = BaseInput();
  const ActionMotionPlan plan = PlayerMotion::BuildActionMotionPlan(clip, input);
  REQUIRE(plan.positions.size() == 20u);
  // The plan follows the clip's local motion (world +x is unchanged here).
  CHECK(plan.positions.back().coords[0] == 0.0f);
  CHECK(plan.positions.back().coords[1] < 0.0f);
  CHECK(plan.outgoing_velocity.coords[1] < 0.0f);
  CHECK(plan.rotation_offset == Approx(0.0f).margin(0.001f));
}

TEST_CASE("A8.0: the plan is deterministic for identical inputs",
          "[player][motion][a8.0]") {
  const AnimationClip clip =
      MakeClip(e_DefString_Movement, 24, -0.05f, Vector3(0, -5.0f, 0), 5.0f,
               0.0f);
  ActionMotionInput input = BaseInput();
  input.parameters.stat_agility = 0.7f;
  input.parameters.stat_acceleration = 0.6f;
  input.parameters.stat_dribble = 0.5f;
  input.parameters.stat_balance = 0.4f;
  input.parameters.stat_technical_ballcontrol = 0.8f;
  input.parameters.last_touch_bias_1000 = 0.25f;
  input.parameters.decaying_position_offset_length = 0.1f;
  const ActionMotionPlan a = PlayerMotion::BuildActionMotionPlan(clip, input);
  const ActionMotionPlan b = PlayerMotion::BuildActionMotionPlan(clip, input);
  REQUIRE(a.positions.size() == b.positions.size());
  for (std::size_t i = 0; i < a.positions.size(); ++i) {
    CHECK(a.positions[i] == b.positions[i]);
  }
  CHECK(a.outgoing_velocity == b.outgoing_velocity);
  CHECK(a.rotation_offset == b.rotation_offset);
}

TEST_CASE("A8.0: Trip with a pass-through trip type still plans its root motion",
          "[player][motion][a8.0]") {
  // trip_type 1 keeps physicsBias at 0.5 instead of 0, so the clip's root
  // motion leaks through; both must produce a same-length plan.
  const AnimationClip clip = MakeClip(e_DefString_Trip, 16, -0.05f,
                                      Vector3(0, -5.0f, 0), 5.0f, 0.0f, -1, 1);
  ActionMotionInput input = BaseInput();
  const ActionMotionPlan plan = PlayerMotion::BuildActionMotionPlan(clip, input);
  CHECK(plan.positions.size() == 16u);
}

TEST_CASE("A8.0: Sliding plans a trajectory from a standstill",
          "[player][motion][a8.0]") {
  const AnimationClip clip = MakeClip(e_DefString_Sliding, 18, -0.05f,
                                      Vector3(0, -5.0f, 0), 5.0f, 0.0f);
  ActionMotionInput input = BaseInput();
  const ActionMotionPlan plan = PlayerMotion::BuildActionMotionPlan(clip, input);
  REQUIRE(plan.positions.size() == 18u);
  CHECK(plan.positions.back().coords[1] < 0.0f);
}

TEST_CASE("A8.0: a non-idle outgoing clip rotates to its native exit direction",
          "[player][motion][a8.0]") {
  // The clip's root motion exits along the clip's outgoing direction, so the
  // requested rotation offset (resulting vs native outgoing) is ~0.
  const Vector3 outgoing(0, -5.0f, 0);
  const AnimationClip clip =
      MakeClip(e_DefString_Movement, 20, -0.05f, outgoing, 5.0f, 0.0f);
  ActionMotionInput input = BaseInput();
  input.use_desired_movement = true;
  input.desired_movement = outgoing;
  const ActionMotionPlan plan = PlayerMotion::BuildActionMotionPlan(clip, input);
  CHECK(plan.rotation_offset == Approx(0.0f).margin(0.01f));
}

TEST_CASE("A8.1: action position sums the frame displacement and smuggle offsets",
          "[player][motion][a8]") {
  const std::vector<Vector3> positions = {Vector3(0, 0, 0), Vector3(0, 1, 0),
                                          Vector3(0, 2, 0)};
  const std::vector<Vector3> roots = {Vector3(0, 0, 0), Vector3(0, -0.05f, 0),
                                      Vector3(0, -0.10f, 0)};
  ActionMotionVectorInput in;
  in.start_position = Vector3(1, 0, 0);
  in.start_angle = 0.0f;
  in.positions = positions;
  in.frame_num = 2;
  in.action_smuggle_offset = Vector3(0.1f, 0, 0);
  in.action_smuggle_sustain_offset = Vector3(0.2f, 0, 0);
  in.movement_smuggle_offset = Vector3(0.3f, 0, 0);
  in.previous_position_2d = Vector3(1, 0, 0);
  in.action_smuggle_movement = Vector3(0.05f, 0, 0);
  in.movement_smuggle_movement = Vector3(0.02f, 0, 0);
  in.position_offset_movement = Vector3(0.1f, 0, 0);
  in.root_positions = roots;

  const ActionMotionVectorResult r = PlayerMotion::ActionMotionVector(in);
  CHECK(r.position.coords[0] == Approx(1.6f));
  CHECK(r.position.coords[1] == Approx(2.0f));
  // actual = (position - previous) * 100
  CHECK(r.actual_movement.coords[0] == Approx(60.0f));
  CHECK(r.actual_movement.coords[1] == Approx(200.0f));
  // physics = actual - actionSmuggle - movementSmuggle - 0.5 * positionOffset
  CHECK(r.physics_movement.coords[0] == Approx(60.0f - 0.05f - 0.02f - 0.05f));
  CHECK(r.physics_movement.coords[1] == Approx(200.0f));
  CHECK(r.movement == r.physics_movement);
  // anim movement comes from the clip's own root positions.
  CHECK(r.anim_movement.coords[1] == Approx(-5.0f));
  CHECK(r.float_velocity == Approx(r.movement.GetLength()));
  CHECK(r.enum_velocity == FloatToEnumVelocity(r.float_velocity));
}

TEST_CASE("A8.1: a missing frame falls back to the baked pose root",
          "[player][motion][a8]") {
  ActionMotionVectorInput in;
  in.start_position = Vector3(0, 0, 0);
  in.start_angle = 0.0f;
  in.positions = {};  // no planned trajectory -> baked pose branch
  in.frame_num = 0;
  in.baked_pose_position = Vector3(0, 1, 0);
  in.previous_position_2d = Vector3(0, 0, 0);

  const ActionMotionVectorResult r = PlayerMotion::ActionMotionVector(in);
  CHECK(r.position.coords[0] == Approx(0.0f));
  CHECK(r.position.coords[1] == Approx(1.0f));
  // With no planned positions, anim movement is the physics movement.
  CHECK(r.anim_movement == r.physics_movement);
  CHECK(r.movement == r.physics_movement);
}

TEST_CASE("A8.1: the baked pose root is rotated by the start angle",
          "[player][motion][a8]") {
  ActionMotionVectorInput in;
  in.start_position = Vector3(0, 0, 0);
  in.start_angle = 0.5f * pi;
  in.positions = {};
  in.frame_num = 0;
  in.baked_pose_position = Vector3(0, 2, 0);  // local +y, rotated 90 deg -> -x

  const ActionMotionVectorResult r = PlayerMotion::ActionMotionVector(in);
  CHECK(r.position.coords[0] == Approx(-2.0f).margin(0.0001f));
  CHECK(r.position.coords[1] == Approx(0.0f).margin(0.0001f));
}

TEST_CASE("A8.3: an idle action uses the baked body direction",
          "[player][motion][a8]") {
  ActionBodyStateInput in;
  in.movement = Vector3(0);
  in.start_angle = 0.0f;
  in.rotation_smuggle_offset = 0.0f;
  in.baked_body_orientation = Quaternion();  // identity -> body +x? z == 0

  const ActionBodyStateResult r = PlayerMotion::ActionBodyState(in);
  CHECK(r.enum_velocity == e_Velocity_Idle);
  CHECK(r.direction_vec == Vector3(0, -1, 0));
  CHECK(r.rel_body_direction_vec == Vector3(0, -1, 0));
  CHECK(r.rel_body_direction_vec_nonquantized == Vector3(0, -1, 0));
  CHECK(r.body_direction_vec == Vector3(0, -1, 0));
}

TEST_CASE("A8.3: a moving action derives direction and quantized body facing",
          "[player][motion][a8]") {
  ActionBodyStateInput in;
  in.movement = Vector3(3, 0, 0);
  in.start_angle = 0.0f;
  in.baked_body_orientation = Quaternion();

  const ActionBodyStateResult r = PlayerMotion::ActionBodyState(in);
  CHECK(r.enum_velocity == FloatToEnumVelocity(3.0f));
  CHECK(r.direction_vec == Vector3(3, 0, 0).GetNormalized());
  CHECK(r.body_direction_vec == r.rel_body_direction_vec.GetRotated2D(r.angle));
  CHECK(r.rel_body_angle ==
        r.rel_body_direction_vec.GetAngle2D(Vector3(0, -1, 0)));
  CHECK(r.rel_body_angle_nonquantized ==
        r.rel_body_direction_vec_nonquantized.GetAngle2D(Vector3(0, -1, 0)));

  // The compatibility body direction is forced onto the allowed set.
  bool on_allowed_set = false;
  for (const Vector3& v : AllowedBodyDirVecs()) {
    if (v == r.rel_body_direction_vec) on_allowed_set = true;
  }
  CHECK(on_allowed_set);
}

TEST_CASE("A9.2: an action smuggle evolves towards its touch frame",
          "[player][motion][a9]") {
  ActionSmuggleTickInput in;
  in.frame_num = 0;
  in.touch_frame = 10;
  in.frame_count = 11;
  in.action_smuggle = Vector3(1, 0, 0);
  in.rotation_smuggle_begin = 0.5f;
  in.rotation_smuggle_end = 1.0f;
  in.allow_pre_touch_rotation_smuggle = false;

  const ActionSmuggleTickResult r = PlayerMotion::ActionSmuggleTick(in);
  // value = cos((0/11 - 0.5) * 2pi) + 1 = 0, then * 0.1 + 0.9 = 0.9
  const float expected = (1.0f / 11.0f) * 0.9f * 100.0f;
  CHECK(r.action_smuggle_movement.coords[0] == Approx(expected));
  CHECK(r.action_smuggle_offset_delta.coords[0] == Approx(expected / 100.0f));
  // No movement smuggle while there is a touch frame.
  CHECK(r.movement_smuggle_movement == Vector3(0));
  // beginBias = 1/11, endBias = 0 (pre-touch).
  CHECK(r.rotation_smuggle_offset == Approx(0.5f * (1.0f - 1.0f / 11.0f)));
}

TEST_CASE("A9.2: a no-touch clip evolves the movement smuggle",
          "[player][motion][a9]") {
  ActionSmuggleTickInput in;
  in.frame_num = 0;
  in.touch_frame = -1;
  in.frame_count = 11;
  in.movement_smuggle = Vector3(1, 0, 0);
  in.rotation_smuggle_begin = 0.5f;
  in.rotation_smuggle_end = 1.0f;

  const ActionSmuggleTickResult r = PlayerMotion::ActionSmuggleTick(in);
  const float expected = (1.0f / 11.0f) * 0.9f * 100.0f;
  CHECK(r.movement_smuggle_movement.coords[0] == Approx(expected));
  CHECK(r.movement_smuggle_offset_delta.coords[0] == Approx(expected / 100.0f));
  CHECK(r.action_smuggle_movement == Vector3(0));
  // cappedFrameBias = 1/11 for both ends.
  CHECK(r.rotation_smuggle_offset ==
        Approx(0.5f * (1.0f - 1.0f / 11.0f) + 1.0f * (1.0f / 11.0f)));
}

TEST_CASE("A9.2: the smuggle evolution is zero after the touch frame",
          "[player][motion][a9]") {
  ActionSmuggleTickInput in;
  in.frame_num = 11;
  in.touch_frame = 10;
  in.frame_count = 12;
  in.action_smuggle = Vector3(1, 0, 0);
  in.movement_smuggle = Vector3(1, 0, 0);

  const ActionSmuggleTickResult r = PlayerMotion::ActionSmuggleTick(in);
  CHECK(r.action_smuggle_movement == Vector3(0));
  CHECK(r.movement_smuggle_movement == Vector3(0));
}

TEST_CASE("A18.4: a non-zero entry still accumulates the whole action smuggle",
          "[player][motion][a18]") {
  constexpr int kTouchFrame = 24;
  constexpr int kStartFrame = 12;
  const Vector3 target(0.4f, -0.2f, 0.0f);

  Vector3 accumulated(0);
  for (int frame = kStartFrame; frame <= kTouchFrame; ++frame) {
    ActionSmuggleTickInput in;
    in.frame_num = frame;
    in.touch_frame = kTouchFrame;
    in.start_frame = kStartFrame;
    in.action_smuggle = target;
    accumulated += PlayerMotion::ActionSmuggleTick(in).action_smuggle_offset_delta;
  }
  // The whole target is delivered by the touch frame.
  CHECK(accumulated.coords[0] == Approx(target.coords[0]).epsilon(0.02));
  CHECK(accumulated.coords[1] == Approx(target.coords[1]).epsilon(0.02));

  // The old absolute-frame basis would only deliver about half of it.
  Vector3 legacy(0);
  for (int frame = kStartFrame; frame <= kTouchFrame; ++frame) {
    float value =
        std::cos((frame / (float)(kTouchFrame + 1) - 0.5f) * football::math::pi * 2.0f) +
        1.0f;
    value = value * 0.1f + 0.9f;
    legacy += target / (float)(kTouchFrame + 1) * value;
  }
  CHECK(legacy.coords[0] < target.coords[0] * 0.7f);
}

TEST_CASE("A18.4: start_frame 0 keeps the legacy action-smuggle formula",
          "[player][motion][a18]") {
  constexpr int kTouchFrame = 24;
  const Vector3 target(0.4f, -0.2f, 0.0f);
  for (int frame = 0; frame <= kTouchFrame; ++frame) {
    ActionSmuggleTickInput in;
    in.frame_num = frame;
    in.touch_frame = kTouchFrame;
    in.action_smuggle = target;
    const ActionSmuggleTickResult r = PlayerMotion::ActionSmuggleTick(in);
    float value =
        std::cos((frame / (float)(kTouchFrame + 1) - 0.5f) * football::math::pi * 2.0f) +
        1.0f;
    value = value * 0.1f + 0.9f;
    const Vector3 expected =
        target / (float)(kTouchFrame + 1) * value * 100.0f;
    // Bit-exact: the default entry frame must not change the float path.
    CHECK(r.action_smuggle_movement.coords[0] == expected.coords[0]);
    CHECK(r.action_smuggle_movement.coords[1] == expected.coords[1]);
  }
}
