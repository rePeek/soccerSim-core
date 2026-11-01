#pragma once

#include <cstdint>
#include <vector>
#include "football/foundation/math/scalar.hpp"

// Runtime identity of an entry in AnimationLibrary. Keep this signed while
// selection still uses -1 as its invalid sentinel.
using AnimationId = int32_t;
static_assert(sizeof(AnimationId) == sizeof(signed int));

// Animation-domain movement buckets and action kinds. These live here (not in
// sim/) so that animation stays free of any sim/ dependency: sim depends on
// these types through animation/.

const float idleVelocity = 0.0f;
const float dribbleVelocity = 3.5f;
const float walkVelocity = 5.0f;
const float sprintVelocity = 8.0f;

const float idleDribbleSwitch = 1.8f;
const float dribbleWalkSwitch = 4.2f;
const float walkSprintSwitch = 6.0f;

// Foot support phases, as an animation-domain fact.
//
// A gait clip knows which foot is carrying weight and when. Player Execution
// needs that answer to decide when a world-space support anchor may be planted
// or released; deriving it from ankle height alone mislabels authored clips (a
// swinging foot can hang lower than a stance foot), and deriving it from the
// settled pose cannot work at all (a locked foot keeps its height).
enum class FootPhase : std::uint8_t {
  Swing = 0,
  // First frame of a contact: the foot touches down this frame.
  Landing = 1,
  // Carrying weight.
  Stance = 2,
  // Last frame of a contact: the foot leaves the ground after this frame.
  Takeoff = 3,
};

struct FootSupportFrame {
  FootPhase left = FootPhase::Swing;
  FootPhase right = FootPhase::Swing;
};

inline bool FootIsSupporting(FootPhase phase) {
  return phase == FootPhase::Landing || phase == FootPhase::Stance ||
         phase == FootPhase::Takeoff;
}

// What the derivation concluded about a whole clip, and whether it may be
// trusted. A gait clip that never carries weight, carries weight the whole time,
// or whose stance foot disagrees wildly with its own root track is a clip the
// derivation could not read - the Player must fall back instead of locking feet
// on a guess.
struct FootSupportSummary {
  // Mean backward speed of the support foot *inside* the clip, per phase. The
  // stance value is the cadence reference playback follows.
  float landing_speed = 0.0f;
  float stance_speed = 0.0f;
  float takeoff_speed = 0.0f;
  // Duty cycles over the cycle, 0..1.
  float support_duty = 0.0f;
  float double_support_duty = 0.0f;
  float flight_duty = 0.0f;
  // A support run shorter than this many frames cannot be a real step.
  int shortest_support_run = 0;
  bool no_support = false;
  bool all_support = false;
  bool stance_too_short = false;
  bool speed_outlier = false;
};

// Trustworthy = there is a track, it has real contact, and it agrees with the
// clip's own travel. Callers use the velocity bridge when this is false.
inline bool FootSupportTrusted(const FootSupportSummary& summary,
                               bool has_track) {
  return has_track && !summary.no_support && !summary.all_support &&
         !summary.stance_too_short && !summary.speed_outlier;
}

// RMA-1: motion quality of a clip, as an animation-domain fact.
//
// The IK-free adaptation only works on a clip whose root track and support
// foot agree: if the stance foot slides inside the animation itself, no
// cadence/stride pairing can remove that slide without moving the world root
// away from the foot. This summary is derived once at load, reports the
// measured slide and loop continuity, and states whether the clip may drive
// the IK-free path. A clip that fails it keeps the compatibility path; the
// flags exist so a bad source GLB is visible instead of silently absorbed.
struct ClipMotionQuality {
  // Mean root-track speed over the clip's samples (m/s).
  float root_speed = 0.0f;
  // The stance-foot speed the support derivation measured (m/s).
  float stance_speed = 0.0f;
  // Horizontal speed of a supporting ankle in the clip's own world track
  // (root track + joint position). Zero is a perfectly planted foot.
  float support_slip_speed = 0.0f;
  float worst_support_slip_speed = 0.0f;
  float seam_root_speed_delta = 0.0f;
  float seam_pose_step = 0.0f;
  // Mean alignment of the stance foot's backward travel with the clip's own
  // root direction, weighted by displacement. A clip whose body is angled
  // relative to its travel can move the foot sideways: that is a pivot, not a
  // calibrated straight gait, and the IK-free root cannot follow it.
  float stance_direction_alignment = 0.0f;
  // The clip has a derived support track to measure against.
  bool tracked = false;
  bool support_consistent = false;
  bool loop_continuous = false;
  // The support track is trustworthy and loops, so the Player may drive the
  // root from the stance-foot speed instead of the clip's own root track. This
  // is the in-memory calibrated reference: it removes the authored root/foot
  // disagreement without editing the GLB.
  bool stance_calibrated = false;
  // The strict source gate: the clip's own root track already agrees with its
  // stance foot, so it needs no calibration at all.
  bool ik_free_usable = false;
};

// A support foot may not slide faster than this inside the clip for the
// IK-free path; beyond it the original animation already contains foot slide.
inline constexpr float kIkFreeMaxSupportSlipSpeed = 0.9f;
// A loop seam may not change the root speed by more than this, or restart the
// pose by more than this joint step, without reading as a pop.
inline constexpr float kIkFreeMaxSeamRootSpeedDelta = 1.5f;
inline constexpr float kIkFreeMaxSeamPoseStep = 0.6f;

enum e_Velocity {
  e_Velocity_Idle,
  e_Velocity_Dribble,
  e_Velocity_Walk,
  e_Velocity_Sprint
};

enum e_FunctionType {
  e_FunctionType_None,
  e_FunctionType_Movement,
  e_FunctionType_BallControl,
  e_FunctionType_Trap,
  e_FunctionType_ShortPass,
  e_FunctionType_LongPass,
  e_FunctionType_HighPass,
  e_FunctionType_Header,
  e_FunctionType_Shot,
  e_FunctionType_Deflect,
  e_FunctionType_Catch,
  e_FunctionType_Interfere,
  e_FunctionType_Trip,
  e_FunctionType_Sliding,
  e_FunctionType_Special
};

// Stable numeric action tags written to .simanim metadata. These are shared by
// the offline source parser and baked runtime selection.
enum e_DefString {
  e_DefString_Empty = 0,
  e_DefString_OutgoingSpecialState = 1,
  e_DefString_IncomingSpecialState = 2,
  e_DefString_SpecialVar1 = 3,
  e_DefString_SpecialVar2 = 4,
  e_DefString_Type = 5,
  e_DefString_Trap = 6,
  e_DefString_Deflect = 7,
  e_DefString_Interfere = 8,
  e_DefString_Trip = 9,
  e_DefString_ShortPass = 10,
  e_DefString_LongPass = 11,
  e_DefString_Shot = 12,
  e_DefString_Sliding = 13,
  e_DefString_Movement = 14,
  e_DefString_Special = 15,
  e_DefString_BallControl = 16,
  e_DefString_HighPass = 17,
  e_DefString_Catch = 18,
  e_DefString_OutgoingRetainState = 19,
  e_DefString_IncomingRetainState = 20,
  e_DefString_Size = 21
};

inline e_FunctionType StringToFunctionType(e_DefString fun) {
  if (fun == e_DefString_Movement) return e_FunctionType_Movement;
  if (fun == e_DefString_BallControl) return e_FunctionType_BallControl;
  if (fun == e_DefString_Trap) return e_FunctionType_Trap;
  if (fun == e_DefString_ShortPass) return e_FunctionType_ShortPass;
  if (fun == e_DefString_LongPass) return e_FunctionType_LongPass;
  if (fun == e_DefString_HighPass) return e_FunctionType_HighPass;
  if (fun == e_DefString_Shot) return e_FunctionType_Shot;
  if (fun == e_DefString_Deflect) return e_FunctionType_Deflect;
  if (fun == e_DefString_Catch) return e_FunctionType_Catch;
  if (fun == e_DefString_Interfere) return e_FunctionType_Interfere;
  if (fun == e_DefString_Trip) return e_FunctionType_Trip;
  if (fun == e_DefString_Sliding) return e_FunctionType_Sliding;
  if (fun == e_DefString_Special) return e_FunctionType_Special;
  return e_FunctionType_None;
}

enum e_Foot {
  e_Foot_Left,
  e_Foot_Right
};

// Stable pose-array order shared by legacy source assets and baked clips.
enum BodyPart {
  middle,
  neck,
  left_thigh,
  right_thigh,
  left_knee,
  right_knee,
  left_ankle,
  right_ankle,
  left_shoulder,
  right_shoulder,
  left_elbow,
  right_elbow,
  body,
  player,
  body_part_max
};

typedef std::vector<int> DataSet;

inline int GetVelocityID(e_Velocity velo, bool treatDribbleAsWalk = false) {
  int id = 0;
  switch (velo) {
    case e_Velocity_Idle:
      id = 0;
      break;
    case e_Velocity_Dribble:
      id = 1;
      break;
    case e_Velocity_Walk:
      id = 2;
      break;
    case e_Velocity_Sprint:
      id = 3;
      break;
    default:
      id = 0;
      break;
  }
  if (treatDribbleAsWalk && id > 1) id--;
  return id;
}

inline float RangeVelocity(float velocity) {
  float ret_velocity = idleVelocity;
  if (velocity >= idleDribbleSwitch && velocity < dribbleWalkSwitch)
    ret_velocity = dribbleVelocity;
  else if (velocity >= dribbleWalkSwitch && velocity < walkSprintSwitch)
    ret_velocity = walkVelocity;
  else if (velocity >= walkSprintSwitch)
    ret_velocity = sprintVelocity;
  return ret_velocity;
}

inline float ClampVelocity(float velocity) {
  if (velocity < 0) return 0;
  if (velocity > sprintVelocity) return sprintVelocity;
  return velocity;
}

inline float EnumToFloatVelocity(e_Velocity velocity) {
  switch (velocity) {
    case e_Velocity_Idle: return idleVelocity;
    case e_Velocity_Dribble: return dribbleVelocity;
    case e_Velocity_Walk: return walkVelocity;
    case e_Velocity_Sprint: return sprintVelocity;
  }
  return 0;
}

inline e_Velocity FloatToEnumVelocity(float velocity) {
  const float ranged_velocity = RangeVelocity(velocity);
  if (ranged_velocity == idleVelocity) return e_Velocity_Idle;
  if (ranged_velocity == dribbleVelocity) return e_Velocity_Dribble;
  if (ranged_velocity == walkVelocity) return e_Velocity_Walk;
  if (ranged_velocity == sprintVelocity) return e_Velocity_Sprint;
  return e_Velocity_Idle;
}