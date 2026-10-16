#ifndef FOOTBALL_SIM_PLAYER_PLAYER_MOTION_HPP
#define FOOTBALL_SIM_PLAYER_PLAYER_MOTION_HPP

#include "football/animation/clip.hpp"
#include "football/animation/motion_clip.hpp"
#include "football/animation/types.hpp"
#include "football/foundation/math/vector3.hpp"
#include "sim/player/player_kinematics.hpp"
#include "sim/player/player_motion_profile.hpp"

#include <span>
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

// ANIM-1 A8 step 1: the execution-side action displacement and movement
// decomposition, relocated from `HumanoidBase::CalculateSpatialState`'s
// non-procedural branch. Pure: it reads only the supplied clip/pose facts.
struct ActionMotionVectorInput {
  Vector3 start_position{0};
  radian start_angle = 0.0f;
  std::span<const Vector3> positions{};   // currentAnim.positions
  int frame_num = 0;
  Vector3 action_smuggle_offset{0};
  Vector3 action_smuggle_sustain_offset{0};
  Vector3 movement_smuggle_offset{0};
  // Baked pose root when `positions` has no entry for `frame_num`.
  Vector3 baked_pose_position{0};
  Vector3 previous_position_2d{0};
  Vector3 action_smuggle_movement{0};
  Vector3 movement_smuggle_movement{0};
  Vector3 position_offset_movement{0};
  std::span<const Vector3> root_positions{};  // clip.root_positions
};

struct ActionMotionVectorResult {
  Vector3 position{0};
  Vector3 actual_movement{0};
  Vector3 physics_movement{0};
  Vector3 anim_movement{0};
  Vector3 movement{0};
  float float_velocity = 0.0f;
  e_Velocity enum_velocity = e_Velocity_Idle;
};

// ANIM-1 A8 step 3: the execution-side body direction and velocity
// quantization for a non-procedural tick, relocated from the body block of
// `HumanoidBase::CalculateSpatialState`. Pure.
struct ActionBodyStateInput {
  Vector3 movement{0};
  radian start_angle = 0.0f;
  radian rotation_smuggle_offset = 0.0f;
  Quaternion baked_body_orientation;
};

struct ActionBodyStateResult {
  Vector3 direction_vec{0, -1, 0};
  radian angle = 0.0f;
  float float_velocity = 0.0f;
  e_Velocity enum_velocity = e_Velocity_Idle;
  Vector3 rel_body_direction_vec{0, -1, 0};
  Vector3 rel_body_direction_vec_nonquantized{0, -1, 0};
  radian rel_body_angle = 0.0f;
  radian rel_body_angle_nonquantized = 0.0f;
  Vector3 body_direction_vec{0, -1, 0};
  radian body_angle = 0.0f;
};

// ANIM-1 A9 step 2: the per-tick action/movement/rotation smuggle evolution,
// relocated from Humanoid::Process. Pure; the caller applies the deltas.
struct ActionSmuggleTickInput {
  int frame_num = 0;
  int touch_frame = -1;
  int frame_count = 0;
  // ANIM-2 A18.4: the action's entry frame. The smuggle is distributed over the
  // frames that actually remain, so a non-zero entry still reaches the target by
  // the touch frame. Default 0 is the legacy absolute-frame formula.
  int start_frame = 0;
  Vector3 action_smuggle{0};
  Vector3 movement_smuggle{0};
  radian rotation_smuggle_begin = 0.0f;
  radian rotation_smuggle_end = 0.0f;
  bool allow_pre_touch_rotation_smuggle = false;
};

struct ActionSmuggleTickResult {
  Vector3 action_smuggle_movement{0};
  Vector3 action_smuggle_offset_delta{0};
  Vector3 movement_smuggle_movement{0};
  Vector3 movement_smuggle_offset_delta{0};
  radian rotation_smuggle_offset = 0.0f;
};

// The single motion interface, with two sources: the continuous locomotion model
// and a baked MotionClip reference.
//
// Exactly one place commits the result (Player). That is what makes an animation
// change unable to move the player by itself, and it is the interface action
// execution calls instead of writing the compatibility spatial state.
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

  // ANIM-1 A8 step 1: the execution-side position/displacement decomposition
  // for a non-procedural (baked action) tick. Pure; the caller commits it.
  static ActionMotionVectorResult ActionMotionVector(
      const ActionMotionVectorInput& input);

  // ANIM-1 A8 step 3: the body direction / velocity quantization of the same
  // non-procedural tick. Pure.
  static ActionBodyStateResult ActionBodyState(
      const ActionBodyStateInput& input);

  // ANIM-1 A9 step 2: the action/movement/rotation smuggle evolution for one
  // tick. Pure.
  static ActionSmuggleTickResult ActionSmuggleTick(
      const ActionSmuggleTickInput& input);
};

#endif  // FOOTBALL_SIM_PLAYER_PLAYER_MOTION_HPP
