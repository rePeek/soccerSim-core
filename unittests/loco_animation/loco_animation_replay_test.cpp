// LOCO-2.7 — headless per-tick off-ball replay across nine body profiles.
//
// Scripts a deterministic clip-switch sequence (idle / walk / sprint / brake /
// turn), drives SelectLocomotionAnimation + AnimationTransitionState + body
// geometry per tick, and verifies: the nine profiles share the same animation
// selection, the blended pose is finite and deterministic, and the body
// geometry scales with height while the motion/animation path stays identical.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "football/animation/library.hpp"
#include "sim/player/animation_transition_state.hpp"
#include "sim/player/player_baked_body_pose.hpp"
#include "sim/player/player_body_profile.hpp"
#include "sim/player/player_kinematics.hpp"
#include "sim/player/player_locomotion_animation.hpp"
#include "sim/player/locomotion_animation_strategy.hpp"

using football::math::Vector3;
using football::player::MakePlayerBodyProfile;
using football::player::PlayerBodyProfile;
using football::player::PlayerDefinition;

namespace {

PlayerBodyProfile Profile(float height, float mass) {
  PlayerDefinition d;
  d.height = height;
  d.weight_kg = mass;
  return MakePlayerBodyProfile(d);
}

PlayerKinematicState State(float speed, const Vector3& dir) {
  PlayerKinematicState s;
  s.velocity = dir.GetNormalized(Vector3(0, -1, 0)) * speed;
  s.facing = dir.GetNormalized(Vector3(0, -1, 0));
  s.bodyFacing = dir.GetNormalized(Vector3(0, -1, 0));
  return s;
}

struct ReplayRow {
  AnimationId clip_id = -1;
  float reference_speed = 0.0f;
  float hip_z = 0.0f;
  float max_joint_jump = 0.0f;
};

std::vector<ReplayRow> Replay(const AnimationLibrary& library,
                              const PlayerBodyProfile& body) {
  // A deterministic speed/direction script.
  struct Step { float speed; Vector3 dir; };
  std::vector<Step> script;
  for (int i = 0; i < 8; ++i) script.push_back({0.0f, Vector3(0, -1, 0)});            // idle
  for (int i = 0; i < 16; ++i) script.push_back({5.0f, Vector3(0, -1, 0)});           // walk
  for (int i = 0; i < 16; ++i) script.push_back({7.5f, Vector3(0, -1, 0)});           // sprint
  for (int i = 0; i < 8; ++i) script.push_back({0.0f, Vector3(0, -1, 0)});            // brake
  const auto side = Vector3(0, -1, 0).GetRotated2D(90.0f * football::math::pi / 180.0f);
  for (int i = 0; i < 16; ++i) script.push_back({5.0f, side});                        // 90 deg turn

  AnimationTransitionState transition;
  PoseFrame previous_final;
  bool have_previous = false;
  int previous_clip = -1;
  int script_frame = 0;

  std::vector<ReplayRow> rows;
  for (const Step& step : script) {
    // Select the clip for this tick's motion (same input for every body).
    auto state = State(step.speed, step.dir);
    LocomotionAnimationInput in;
    in.current = state;
    in.next = state;
    in.desired_velocity = step.dir * step.speed;
    in.body = body;
    const auto choice = SelectLocomotionAnimation(in, library.Clips());
    REQUIRE(choice.animation_id >= 0);
    const AnimationClip& clip = library.Get(static_cast<uint32_t>(choice.animation_id));

    // Advance the target clip's integer frame.
    const int frame = script_frame % static_cast<int>(clip.poses.size());
    const PoseFrame target = clip.poses[frame];

    PoseFrame final;
    if (choice.animation_id != previous_clip) {
      if (have_previous) {
        transition.Begin(previous_final, 0.0f, kDefaultMovementBlendTicks);
        final = transition.Advance(target, 0.0f);
      } else {
        transition.Reset();
        final = target;
      }
    } else {
      final = transition.Advance(target, 0.0f);
    }

    // Continuity is measured BEFORE advancing previous_final, so `final` is
    // compared against the pose produced at the end of the previous tick.
    float jump = 0.0f;
    if (have_previous) {
      for (std::size_t b = 0; b < kBodyPartCount; ++b) {
        jump = std::max(jump, static_cast<float>(final.orientations[b].GetRotationAngle(
                             previous_final.orientations[b])));
      }
    }
    previous_final = final;
    have_previous = true;
    previous_clip = choice.animation_id;

    // Body geometry follows the final pose and the body profile.
    const PlayerKinematicState at_origin;
    const auto world = EvaluateBakedBodyPose(final, at_origin, 0.0f, body);

    ReplayRow row;
    row.clip_id = choice.animation_id;
    row.reference_speed = choice.reference_speed;
    row.hip_z = world.joints[BodyPart::body].coords[2];
    row.max_joint_jump = jump;
    rows.push_back(row);
    ++script_frame;
  }
  return rows;
}

}  // namespace

TEST_CASE("nine profiles replay deterministically with continuous poses") {
  AnimationLibrary library;
  REQUIRE(library.Load(GFOOTBALL_BAKED_ANIM_PATH));

  const std::vector<PlayerBodyProfile> bodies{
      Profile(1.65f, 53.0f), Profile(1.65f, 63.0f), Profile(1.65f, 78.0f),
      Profile(1.80f, 64.0f), Profile(1.80f, 75.0f), Profile(1.80f, 90.0f),
      Profile(1.95f, 75.0f), Profile(1.95f, 88.0f), Profile(1.95f, 105.0f)};

  std::vector<std::vector<ReplayRow>> runs;
  for (const auto& body : bodies) runs.push_back(Replay(library, body));

  // Same animation selection for every body.
  for (std::size_t i = 1; i < runs.size(); ++i) {
    REQUIRE(runs[i].size() == runs[0].size());
    for (std::size_t t = 0; t < runs[0].size(); ++t) {
      REQUIRE(runs[i][t].clip_id == runs[0][t].clip_id);
    }
  }

  // Deterministic and finite: replay the first body and compare bit-exactly.
  const auto again = Replay(library, bodies[0]);
  REQUIRE(again.size() == runs[0].size());
  for (std::size_t t = 0; t < again.size(); ++t) {
    REQUIRE(again[t].clip_id == runs[0][t].clip_id);
    REQUIRE(again[t].hip_z == runs[0][t].hip_z);
  }

  // Continuity: measured per joint between consecutive final poses. Observed
  // max is ~0.40 rad (23 deg) across real idle/walk/sprint/brake/turn
  // switches; a genuine teleport would be near pi. 1.0 rad is a loose but
  // meaningful no-teleport bound.
  for (const auto& run : runs) {
    for (const auto& row : run) {
      REQUIRE(std::isfinite(row.hip_z));
      REQUIRE(std::isfinite(row.max_joint_jump));
      REQUIRE(row.max_joint_jump < 1.0f);
    }
  }

  // Height scales geometry, not selection: the 1.95 m profile's hip is higher
  // than the 1.65 m profile's hip at every tick.
  for (std::size_t t = 0; t < runs[0].size(); ++t) {
    REQUIRE(runs[6][t].hip_z > runs[0][t].hip_z);
  }

  if (std::getenv("FOOTBALL_LOCO_REPLAY_DIAG") != nullptr) {
    std::fprintf(stderr, "tick,clip,ref,hip_z,max_jump\n");
    for (std::size_t t = 0; t < runs[0].size(); ++t) {
      std::fprintf(stderr, "%zu,%d,%.2f,%.3f,%.3f\n", t, runs[0][t].clip_id,
                   runs[0][t].reference_speed, runs[0][t].hip_z,
                   runs[0][t].max_joint_jump);
    }
  }
}