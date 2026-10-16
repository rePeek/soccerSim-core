// ANIM-1 A6.3b.2 / ANIM-2 A12.2 — the legacy Movement candidate refinement, as a
// stateless Animation function.
//
// This is a line-for-line relocation of the Movement path of
// `HumanoidBase::_KeepBestDirectionAnims` / `_KeepBestBodyDirectionAnims` and
// the Movement-relevant part of the stable-sort chain in `Humanoid::SelectAnim`
// (plus the config arrays and similarity ratings they read). The behaviour is
// intended to be bit-identical: same sorting order, same floating-point
// comparisons, same candidate-set order, same erase semantics.
//
// It depends only on the AnimationLibrary metadata and the explicit request.
// No Player / Humanoid / Simulation / Ball / RNG.
//
// ANIM-2 A12.2: only the request structure and the functions callers actually
// use are declared here. The internal sorts, filters and rating scratch live in
// src/animation/movement_selection.cpp.

#ifndef FOOTBALL_ANIMATION_MOVEMENT_SELECTION_HPP
#define FOOTBALL_ANIMATION_MOVEMENT_SELECTION_HPP

#include <span>
#include <vector>

#include "football/animation/library.hpp"
#include "football/animation/types.hpp"

// Moved verbatim from humanoidbase.cpp (config data). Declared here because the
// movement-vector snap helpers and the refinement tests read them.
const std::vector<Vector3>& AllowedBodyDirVecs();
const std::vector<Vector3>& PreferredDirectionVecs();

Vector3 ForceIntoAllowedBodyDirectionVec(const Vector3& src);
radian ForceIntoAllowedBodyDirectionAngle(radian angle);
Vector3 ForceIntoPreferredDirectionVec(const Vector3& src);
radian ForceIntoPreferredDirectionAngle(radian angle);

struct MovementRefinementRequest {
  // Command / spatial inputs, all already resolved by the caller.
  Vector3 rel_desired_direction;                       // desiredDirection rotated by -angle
  e_Velocity desired_velocity = e_Velocity_Idle;       // FloatToEnumVelocity(desiredVelocityFloat)
  float cornering_bias = 0.0f;                         // predicate_CorneringBias
  Vector3 look_at;                                     // desiredLookAt (world)
  Vector3 position;                                    // spatialState.position
  radian angle = 0.0f;                                 // spatialState.angle
  Vector3 rel_incoming_body_direction;                 // spatialState.relBodyDirectionVec
  e_Velocity incoming_velocity = e_Velocity_Idle;      // spatialState.enumVelocity
  e_Foot desired_foot = e_Foot_Left;                   // spatialState.foot
  float idle_level = 0.0f;                             // desiredIdleLevel
  bool use_desired_look_at = false;
  bool is_movement = true;                             // functionType == Movement
  bool is_ball_control = false;                        // functionType == BallControl
};

// Full refinement: strict direction filter (+ optional body filter), then the
// stable sort chain. The Movement pick is `front()`.
std::vector<int> RefineMovementCandidates(const AnimationLibrary& library,
                                          std::span<const int> crude,
                                          const MovementRefinementRequest& request,
                                          std::vector<int>* without_foot_out = nullptr);

// ANIM-1 A6.4 step 1: the non-Movement refinement (Trip / Special / Interfere /
// Sliding shape). Same pipeline as Movement but with the caller's strictness and
// the baseanim/trip tie-breaks the legacy chain applies for non-Movement.
std::vector<int> RefineNonMovementCandidates(
    const AnimationLibrary& library, std::span<const int> crude,
    const MovementRefinementRequest& request, bool strict, radian allowed_angle,
    int allowed_velocity_steps, bool use_desired_trip_direction,
    const Vector3& rel_desired_trip_direction);

// ANIM-2 A10.3b: the legacy `useDesiredMovement == false` chain. No direction
// filter runs (the legacy chain applied that only when the command carried
// desired movement), but everything else matches order-for-order. When
// `without_foot_out` is non-null it receives the same chain minus the foot
// sort, for the foot counterfactual diagnostic.
std::vector<int> RefineLegacyCandidates(
    const AnimationLibrary& library, std::span<const int> crude,
    const MovementRefinementRequest& request, e_FunctionType function_type,
    bool use_desired_trip_direction, const Vector3& rel_desired_trip_direction,
    std::vector<int>* without_foot_out = nullptr);

// ANIM-2 A10.3c: the idle Movement clip, relocated from
// Player::GetIdleMovementAnimID. Pure over the library and the pose facts.
AnimationId SelectIdleMovementAnim(const AnimationLibrary& library,
                                   const Vector3& position, radian angle,
                                   float float_velocity);

// ANIM-2 A10.3c: the retain-animation clip, relocated from
// Player::SelectRetainAnim. It sorts by the legacy movement similarity with
// the caller's predicate values, exactly as the Player used to.
AnimationId SelectRetainAnimClip(const AnimationLibrary& library,
                                 const Vector3& rel_desired_direction,
                                 e_Velocity desired_velocity,
                                 float cornering_bias);

#endif  // FOOTBALL_ANIMATION_MOVEMENT_SELECTION_HPP
