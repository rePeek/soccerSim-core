#ifndef FOOTBALL_SIM_PLAYER_PLAYER_MOTION_HPP
#define FOOTBALL_SIM_PLAYER_PLAYER_MOTION_HPP

#include "football/animation/clip.hpp"
#include "football/animation/motion_clip.hpp"
#include "football/animation/types.hpp"
#include "football/foundation/math/vector3.hpp"
#include "sim/player/player_kinematics.hpp"
#include "sim/player/player_motion_profile.hpp"

#include <vector>

struct MotionSample {
  football::math::Vector3 displacement = football::math::Vector3(0);
  football::math::Vector3 velocity = football::math::Vector3(0);
};

// A completed motion step. `state` is the proposed next kinematic state, so the
// committer (Player) does not have to re-integrate; `sample` stays readable for
// callers that only need the proposal.
struct MotionStep {
  PlayerKinematicState state;
  MotionSample sample;
};

// ANIM-1 A6.4 A8.0: the kinematic snapshot and capability inputs a candidate
// reference-motion plan needs. No Player, Humanoid, Ball or RNG pointer.
struct ActionMotionParameters {
  float max_velocity = 0.0f;
  float stat_agility = 0.0f;
  float stat_acceleration = 0.0f;
  float stat_velocity = 0.0f;
  float stat_dribble = 0.0f;
  float stat_balance = 0.0f;
  float stat_technical_ballcontrol = 0.0f;
  // Precomputed Player::GetLastTouchBias(1000, now) and the decaying position
  // offset length, so the kernel does not read Player or a timeline instant.
  float last_touch_bias_1000 = 0.0f;
  float decaying_position_offset_length = 0.0f;
};

struct ActionMotionInput {
  Vector3 spatial_movement{0};
  radian angle = 0.0f;
  Vector3 direction_vec{0, -1, 0};
  float float_velocity = 0.0f;
  bool use_desired_movement = false;
  Vector3 desired_movement{0};
  bool use_desired_body_direction = false;
  Vector3 desired_body_direction_rel{0, -1, 0};
  ActionMotionParameters parameters;
};

// The adapted reference trajectory of one candidate action. This is a motion
// *plan*: it never commits a kinematic state or touches the Ball.
struct ActionMotionPlan {
  std::vector<Vector3> positions;
  Vector3 outgoing_velocity{0};
  radian rotation_offset = 0.0f;
};

// The single motion interface, with two sources: the continuous locomotion model
// and a baked MotionClip reference.
//
// Exactly one place commits the result (Player). That is what makes an animation
// change unable to move the player by itself, and it is the interface action
// execution will later call instead of writing Humanoid's spatial state.
class PlayerMotion {
 public:
  // Continuous model. A facade over PlayerLocomotion, not a second
  // implementation: it steps a copy of the state and reports the step.
  static MotionStep StepContinuous(const PlayerKinematicState& current,
                                   const PlayerMotionProfile& profile,
                                   const football::math::Vector3& desired_velocity,
                                   const football::math::Vector3& idle_facing,
                                   float dt);
  // Proposal-only view of StepContinuous, for callers that do not commit.
  static MotionSample SampleContinuous(const PlayerKinematicState& current,
                                       const PlayerMotionProfile& profile,
                                       const football::math::Vector3& desired_velocity,
                                       const football::math::Vector3& idle_facing,
                                       float dt);

  // Baked reference. `frame` is a fractional clip frame, so a clip playing at a
  // non-unit rate is sampled without touching the clip.
  static football::math::Vector3 ClipDisplacement(const MotionClip& clip,
                                                  float frame);
  static MotionSample SampleClip(const MotionClip& clip, float frame, float dt);

  // Maps the four capability fields onto the locomotion parameter set. The
  // remaining locomotion parameters keep their defaults until the motion
  // authority move (P2) needs more of them.
  static PlayerLocomotionParameters LocomotionParameters(
      const PlayerMotionProfile& profile);

  // ANIM-1 A6.4 A8.0: the pure candidate reference-motion kernel. It is the
  // selection-side calculation relocated line-for-line from
  // `HumanoidBase::CalculatePhysicsVector`; the Humanoid adapter only
  // assembles the input and writes the output back.
  static ActionMotionPlan BuildActionMotionPlan(const AnimationClip& clip,
                                                const ActionMotionInput& input);
};

#endif  // FOOTBALL_SIM_PLAYER_PLAYER_MOTION_HPP
