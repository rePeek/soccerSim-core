#ifndef FOOTBALL_ANIMATION_MOVEMENT_SELECTION_HPP
#define FOOTBALL_ANIMATION_MOVEMENT_SELECTION_HPP

#include <algorithm>
#include <cmath>
#include <span>
#include <vector>

#include "football/animation/library.hpp"
#include "football/animation/movement_math.hpp"
#include "football/animation/selector.hpp"

// ANIM-1 A6.3b.2 — the legacy Movement candidate refinement, as a stateless
// Animation function.
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



// Moved verbatim from humanoidbase.cpp (file-local config data).
inline const std::vector<Vector3>& AllowedBodyDirVecs() {
  static const std::vector<Vector3> v = {
      Vector3(0, -1, 0),
      Vector3(0, -1, 0).GetRotated2D(-0.25f * pi),
      Vector3(0, -1, 0).GetRotated2D(0.25f * pi),
      Vector3(0, -1, 0).GetRotated2D(-0.75f * pi),
      Vector3(0, -1, 0).GetRotated2D(0.75f * pi)};
  return v;
}

inline const std::vector<radian>& AllowedBodyDirAngles() {
  static const std::vector<radian> v = {0 * pi, 0.25f * pi, -0.25f * pi,
                                        0.75f * pi, -0.75f * pi};
  return v;
}

inline const std::vector<Vector3>& PreferredDirectionVecs() {
  static const std::vector<Vector3> v = {
      Vector3(0, -1, 0),
      Vector3(0, -1, 0).GetRotated2D(0.111f * pi),
      Vector3(0, -1, 0).GetRotated2D(-0.111f * pi),
      Vector3(0, -1, 0).GetRotated2D(0.25f * pi),
      Vector3(0, -1, 0).GetRotated2D(-0.25f * pi),
      Vector3(0, -1, 0).GetRotated2D(0.5f * pi),
      Vector3(0, -1, 0).GetRotated2D(-0.5f * pi),
      Vector3(0, -1, 0).GetRotated2D(0.75f * pi),
      Vector3(0, -1, 0).GetRotated2D(-0.75f * pi),
      Vector3(0, -1, 0).GetRotated2D(0.999f * pi),
      Vector3(0, -1, 0).GetRotated2D(-0.999f * pi)};
  return v;
}

inline const std::vector<radian>& PreferredDirectionAngles() {
  static const std::vector<radian> v = {
      0 * pi,      0.111f * pi, -0.111f * pi, 0.25f * pi, -0.25f * pi,
      0.5f * pi,   -0.5f * pi,  0.75f * pi,   -0.75f * pi, 0.999f * pi,
      -0.999f * pi};
  return v;
}

inline Vector3 ForceIntoAllowedBodyDirectionVec(const Vector3& src) {
  float bestDot = -1.0f;
  Vector3 best;
  for (const Vector3& vec : AllowedBodyDirVecs()) {
    float nDotL = vec.GetDotProduct(src);
    if (nDotL > bestDot) {
      bestDot = nDotL;
      best = vec;
    }
  }
  return best;
}

inline radian ForceIntoAllowedBodyDirectionAngle(radian angle) {
  float bestAngleDiff = 10000.0;
  radian bestValue = 0;
  for (radian v : AllowedBodyDirAngles()) {
    float diff = std::fabs(v - angle);
    if (diff < bestAngleDiff) {
      bestAngleDiff = diff;
      bestValue = v;
    }
  }
  return bestValue;
}

inline Vector3 ForceIntoPreferredDirectionVec(const Vector3& src) {
  float bestDot = -1.0f;
  Vector3 bestValue;
  for (const Vector3& v : PreferredDirectionVecs()) {
    float nDotL = v.GetDotProduct(src);
    if (nDotL > bestDot) {
      bestDot = nDotL;
      bestValue = v;
    }
  }
  return bestValue;
}

inline radian ForceIntoPreferredDirectionAngle(radian angle) {
  float bestAngleDiff = 10000.0;
  radian bestValue = 0;
  for (radian v : PreferredDirectionAngles()) {
    float diff = std::fabs(v - angle);
    if (diff < bestAngleDiff) {
      bestAngleDiff = diff;
      bestValue = v;
    }
  }
  return bestValue;
}

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

// Movement similarity, moved verbatim from HumanoidBase::GetMovementSimilarity.
inline float MovementSimilarity(const AnimationClip& clip,
                                const Vector3& rel_desired_direction,
                                e_Velocity desired_velocity,
                                float cornering_bias) {
  Vector3 desiredMovement = rel_desired_direction * EnumToFloatVelocity(desired_velocity);

  Vector3 outgoingDirection = ForceIntoPreferredDirectionVec(clip.metadata.outgoing_direction);
  float outgoingVelocity = RangeVelocity(clip.metadata.outgoing_velocity);
  Vector3 outgoingMovement = outgoingDirection * outgoingVelocity;

  desiredMovement = desiredMovement * (1.0f - cornering_bias);

  float value = (desiredMovement - outgoingMovement).GetLength();
  value -= std::fabs(rel_desired_direction.GetDotProduct(outgoingDirection)) * 4.0f;
  return value;
}

// Body-direction rating, moved verbatim from HumanoidBase::DirectionSimilarityRating.
inline real DirectionSimilarityRating(const AnimationClip& clip,
                                      const Vector3& look_at,
                                      const Vector3& position,
                                      radian angle,
                                      const Vector3& rel_desired_direction) {
  Vector3 relDesiredBodyDirection1 =
      ((look_at - position).GetRotated2D(-angle) - clip.metadata.translation)
          .GetNormalized(Vector3(0, -1, 0));
  radian maxAngleSmuggle = 0.1f * pi;
  radian outgoingAngle1 =
      clip.metadata.outgoing_direction
          .GetRotated2D(clamp(rel_desired_direction.GetAngle2D(clip.metadata.outgoing_direction),
                              -maxAngleSmuggle, maxAngleSmuggle))
          .GetAngle2D(Vector3(0, -1, 0));
  Vector3 predictedOutgoingBodyDirection1 =
      Vector3(0, -1, 0).GetRotated2D(clip.metadata.outgoing_body_angle).GetRotated2D(outgoingAngle1);
  radian rating1 = std::fabs(predictedOutgoingBodyDirection1.GetAngle2D(relDesiredBodyDirection1));
  rating1 += std::fabs(clip.metadata.outgoing_body_angle) * 0.05f;
  return rating1;
}

namespace movement_selection_detail {

inline void StableSortByRating(const AnimationLibrary& library,
                               std::vector<int>& ids,
                               const std::vector<float>& rating) {
  std::stable_sort(ids.begin(), ids.end(), [&](int a, int b) {
    return rating[static_cast<std::size_t>(a)] < rating[static_cast<std::size_t>(b)];
  });
}

inline std::vector<float> RatingScratch(const AnimationLibrary& library) {
  return std::vector<float>(library.Clips().size(), 0.0f);
}

}  // namespace movement_selection_detail

// The legacy direction filter. `strict` selects the Movement/Interfere-strict
// form; the non-strict form keeps candidates within `allowed_angle` /
// `allowed_velocity_steps` of the best quadrant (last-ditch anims may always
// change velocity). Moved verbatim from HumanoidBase::_KeepBestDirectionAnims.
inline void SortByBaseanim(const AnimationLibrary& library, std::vector<int>& data_set) {
  std::stable_sort(data_set.begin(), data_set.end(), [&](int a, int b) {
    const bool is_base_a = library.Get(static_cast<uint32_t>(a)).metadata.base_animation;
    const bool is_base_b = library.Get(static_cast<uint32_t>(b)).metadata.base_animation;
    if (is_base_a && !is_base_b) return true;
    return false;
  });
}

inline void KeepBestDirectionAnims(const AnimationLibrary& library,
                                   std::vector<int>& data_set,
                                   const MovementRefinementRequest& request,
                                   bool strict = true, radian allowed_angle = 0.0f,
                                   int allowed_velocity_steps = 0,
                                   int forced_quadrant_id = -1) {
  if (data_set.empty()) return;

  int best_quadrant_id = forced_quadrant_id;
  if (best_quadrant_id == -1) {
    std::vector<float> order = movement_selection_detail::RatingScratch(library);
    for (int anim : data_set) {
      order[static_cast<std::size_t>(anim)] =
          MovementSimilarity(library.Get(static_cast<uint32_t>(anim)),
                             request.rel_desired_direction, request.desired_velocity,
                             request.cornering_bias);
    }
    movement_selection_detail::StableSortByRating(library, data_set, order);

    // The best anim should be a baseanim for non-Movement strict selection.
    if (strict && !request.is_movement) SortByBaseanim(library, data_set);

    best_quadrant_id = library.Get(static_cast<uint32_t>(data_set.front())).metadata.quadrant;
  }

  auto iter = data_set.begin();
  ++iter;
  while (iter != data_set.end()) {
    if (strict) {
      if (library.Get(static_cast<uint32_t>(*iter)).metadata.quadrant == best_quadrant_id) {
        ++iter;
      } else {
        iter = data_set.erase(iter);
      }
    } else {
      const int quadrant_id = library.Get(static_cast<uint32_t>(*iter)).metadata.quadrant;
      const Quadrant& best_quadrant = BakedAnimationSelector::GetQuadrant(best_quadrant_id);
      const Quadrant& quadrant = BakedAnimationSelector::GetQuadrant(quadrant_id);

      bool predicate = true;
      if (!library.Get(static_cast<uint32_t>(*iter)).metadata.last_ditch) {
        if (std::abs(GetVelocityID(quadrant.velocity, true) -
                     GetVelocityID(best_quadrant.velocity, true)) > allowed_velocity_steps)
          predicate = false;
      }
      if (std::fabs(quadrant.angle - best_quadrant.angle) > allowed_angle) predicate = false;

      if (predicate) {
        ++iter;
      } else {
        iter = data_set.erase(iter);
      }
    }
  }
}

// The legacy body-direction filter. `adaptedAllowedAngle` is `0.06f * pi` when
// strict, else the caller's `allowed_angle`.
inline void KeepBestBodyDirectionAnims(const AnimationLibrary& library,
                                       std::vector<int>& data_set,
                                       const MovementRefinementRequest& request,
                                       bool strict = true, radian allowed_angle = 0.0f) {
  if (data_set.empty()) return;

  std::vector<float> order = movement_selection_detail::RatingScratch(library);
  for (int anim : data_set) {
    order[static_cast<std::size_t>(anim)] = static_cast<float>(DirectionSimilarityRating(
        library.Get(static_cast<uint32_t>(anim)), request.look_at, request.position,
        request.angle, request.rel_desired_direction));
  }
  movement_selection_detail::StableSortByRating(library, data_set, order);

  if (strict && !request.is_movement) SortByBaseanim(library, data_set);

  const AnimationClip& best_clip = library.Get(static_cast<uint32_t>(data_set.front()));
  radian best_outgoing_body_angle =
      ForceIntoAllowedBodyDirectionAngle(best_clip.metadata.outgoing_body_angle);
  radian best_outgoing_angle = ForceIntoPreferredDirectionAngle(best_clip.metadata.outgoing_angle);
  radian best_look_angle = best_outgoing_body_angle + best_outgoing_angle;

  auto iter = data_set.begin();
  ++iter;
  while (iter != data_set.end()) {
    const AnimationClip& anim_clip = library.Get(static_cast<uint32_t>(*iter));
    radian anim_outgoing_body_angle =
        ForceIntoAllowedBodyDirectionAngle(anim_clip.metadata.outgoing_body_angle);
    radian anim_outgoing_angle =
        ForceIntoPreferredDirectionAngle(anim_clip.metadata.outgoing_angle);
    radian anim_look_angle = anim_outgoing_body_angle + anim_outgoing_angle;

    const float adapted_allowed_angle = strict ? (0.06f * pi) : static_cast<float>(allowed_angle);
    if (std::fabs(anim_look_angle - best_look_angle) <= adapted_allowed_angle) {
      ++iter;
    } else {
      iter = data_set.erase(iter);
    }
  }
}

// The Movement-relevant part of the stable-sort chain in SelectAnim.
inline void SortByPriority(const AnimationLibrary& library, std::vector<int>& data_set) {
  std::stable_sort(data_set.begin(), data_set.end(), [&](int a, int b) {
    return std::fabs(library.Get(static_cast<uint32_t>(a)).metadata.priority) <
           std::fabs(library.Get(static_cast<uint32_t>(b)).metadata.priority);
  });
}

inline void SortByIdle(const AnimationLibrary& library, std::vector<int>& data_set,
                       float idle_level) {
  std::stable_sort(data_set.begin(), data_set.end(), [&](int a, int b) {
    return std::fabs(library.Get(static_cast<uint32_t>(a)).metadata.idle_level - idle_level) <
           std::fabs(library.Get(static_cast<uint32_t>(b)).metadata.idle_level - idle_level);
  });
}

inline void SortByFoot(const AnimationLibrary& library, std::vector<int>& data_set,
                       e_Foot desired_foot) {
  std::stable_sort(data_set.begin(), data_set.end(), [&](int a, int b) {
    const AnimationClip& c1 = library.Get(static_cast<uint32_t>(a));
    const AnimationClip& c2 = library.Get(static_cast<uint32_t>(b));
    int one = 1;
    int two = 1;
    if (static_cast<e_Foot>(c1.metadata.current_foot) == desired_foot) one = 0;
    if (static_cast<e_Foot>(c2.metadata.current_foot) == desired_foot) two = 0;
    if (FloatToEnumVelocity(c1.metadata.incoming_velocity) == e_Velocity_Idle) one = 0;
    if (FloatToEnumVelocity(c2.metadata.incoming_velocity) == e_Velocity_Idle) two = 0;
    return one < two;
  });
}

inline void SortByIncomingBodyDirection(const AnimationLibrary& library, std::vector<int>& data_set,
                                        const Vector3& rel_incoming_body_direction) {
  std::stable_sort(data_set.begin(), data_set.end(), [&](int a, int b) {
    const AnimationClip& c1 = library.Get(static_cast<uint32_t>(a));
    const AnimationClip& c2 = library.Get(static_cast<uint32_t>(b));
    float rating1 = std::fabs(
        ForceIntoAllowedBodyDirectionVec(c1.metadata.incoming_body_direction)
            .GetAngle2D(ForceIntoAllowedBodyDirectionVec(rel_incoming_body_direction))) /
        pi;
    float rating2 = std::fabs(
        ForceIntoAllowedBodyDirectionVec(c2.metadata.incoming_body_direction)
            .GetAngle2D(ForceIntoAllowedBodyDirectionVec(rel_incoming_body_direction))) /
        pi;
    if (FloatToEnumVelocity(c1.metadata.incoming_velocity) == e_Velocity_Idle) rating1 = 0;
    if (FloatToEnumVelocity(c2.metadata.incoming_velocity) == e_Velocity_Idle) rating2 = 0;
    return rating1 < rating2;
  });
}

inline void SortByIncomingVelocity(const AnimationLibrary& library, std::vector<int>& data_set,
                                   e_Velocity incoming_velocity) {
  std::stable_sort(data_set.begin(), data_set.end(), [&](int a, int b) {
    const int current_velocity_id = GetVelocityID(incoming_velocity);
    const int a_id = GetVelocityID(FloatToEnumVelocity(
        library.Get(static_cast<uint32_t>(a)).metadata.incoming_velocity));
    const int b_id = GetVelocityID(FloatToEnumVelocity(
        library.Get(static_cast<uint32_t>(b)).metadata.incoming_velocity));
    float rating1 = std::fabs(clamp(a_id - current_velocity_id, -3, 3));
    float rating2 = std::fabs(clamp(b_id - current_velocity_id, -3, 3));
    return rating1 < rating2;
  });
}

// The Movement-relevant part of the stable-sort chain in SelectAnim. Movement
// runs no baseanim, deflect or trip sort. When `without_foot_out` is non-null it
// also receives the same chain minus the foot tie-break, which is exactly what
// the legacy foot counterfactual measured.
inline void SortMovementCandidates(const AnimationLibrary& library,
                                   std::vector<int>& data_set,
                                   const MovementRefinementRequest& request,
                                   std::vector<int>* without_foot_out = nullptr) {
  SortByPriority(library, data_set);
  SortByIdle(library, data_set, request.idle_level);
  if (without_foot_out != nullptr) *without_foot_out = data_set;
  SortByFoot(library, data_set, request.desired_foot);
  SortByIncomingBodyDirection(library, data_set, request.rel_incoming_body_direction);
  SortByIncomingVelocity(library, data_set, request.incoming_velocity);
  if (without_foot_out != nullptr) {
    SortByIncomingBodyDirection(library, *without_foot_out, request.rel_incoming_body_direction);
    SortByIncomingVelocity(library, *without_foot_out, request.incoming_velocity);
  }
}

// Full refinement: strict direction filter (+ optional body filter), then the
// stable sort chain. The Movement pick is `front()`.
inline std::vector<int> RefineMovementCandidates(const AnimationLibrary& library,
                                                 std::span<const int> crude,
                                                 const MovementRefinementRequest& request,
                                                 std::vector<int>* without_foot_out = nullptr) {
  std::vector<int> data_set(crude.begin(), crude.end());
  if (data_set.empty()) {
    if (without_foot_out != nullptr) without_foot_out->clear();
    return data_set;
  }

  KeepBestDirectionAnims(library, data_set, request);
  if (request.use_desired_look_at) KeepBestBodyDirectionAnims(library, data_set, request);
  SortMovementCandidates(library, data_set, request, without_foot_out);
  return data_set;
}




// The legacy trip-direction tie-break (OLD METHOD, kept as-is).
inline void SortByTripDirection(const AnimationLibrary& library, std::vector<int>& data_set,
                                const Vector3& rel_desired_trip_direction) {
  std::stable_sort(data_set.begin(), data_set.end(), [&](int a, int b) {
    const float rating1 = -library.Get(static_cast<uint32_t>(a))
                               .metadata.bump_direction.GetDotProduct(rel_desired_trip_direction);
    const float rating2 = -library.Get(static_cast<uint32_t>(b))
                               .metadata.bump_direction.GetDotProduct(rel_desired_trip_direction);
    return rating1 < rating2;
  });
}

// ANIM-1 A6.4 step 1: the non-Movement refinement (Trip / Special / Interfere /
// Sliding shape). Same pipeline as Movement but with the caller's strictness and
// the baseanim/trip tie-breaks the legacy chain applies for non-Movement.
inline std::vector<int> RefineNonMovementCandidates(
    const AnimationLibrary& library, std::span<const int> crude,
    const MovementRefinementRequest& request, bool strict, radian allowed_angle,
    int allowed_velocity_steps, bool use_desired_trip_direction,
    const Vector3& rel_desired_trip_direction) {
  std::vector<int> data_set(crude.begin(), crude.end());
  if (data_set.empty()) return data_set;

  KeepBestDirectionAnims(library, data_set, request, strict, allowed_angle,
                         allowed_velocity_steps);
  if (request.use_desired_look_at) {
    KeepBestBodyDirectionAnims(library, data_set, request, strict, allowed_angle);
  }

  SortByPriority(library, data_set);
  SortByIdle(library, data_set, request.idle_level);
  SortByFoot(library, data_set, request.desired_foot);
  if (!request.is_ball_control) {
    SortByIncomingBodyDirection(library, data_set, request.rel_incoming_body_direction);
  }
  SortByIncomingVelocity(library, data_set, request.incoming_velocity);
  if (use_desired_trip_direction) {
    SortByTripDirection(library, data_set, rel_desired_trip_direction);
  }
  if (!request.is_movement) SortByBaseanim(library, data_set);
  return data_set;
}

// The legacy catch/deflect tie-break: prefer a clip that can retain the ball.
inline void SortByCatchOrDeflect(const AnimationLibrary& library,
                                  std::vector<int>& data_set) {
  std::stable_sort(data_set.begin(), data_set.end(), [&](int a, int b) {
    const bool catch_a =
        !library.Get(static_cast<uint32_t>(a)).metadata.outgoing_retain_state.empty();
    const bool catch_b =
        !library.Get(static_cast<uint32_t>(b)).metadata.outgoing_retain_state.empty();
    if (catch_a && !catch_b) return true;
    return false;
  });
}

// Body-direction similarity sort (used by the idle selection).
inline void SortByBodyDirectionSimilarity(const AnimationLibrary& library,
                                          std::vector<int>& data_set,
                                          const Vector3& look_at,
                                          const Vector3& position, radian angle,
                                          const Vector3& rel_desired_direction) {
  std::stable_sort(data_set.begin(), data_set.end(), [&](int a, int b) {
    return DirectionSimilarityRating(library.Get(static_cast<uint32_t>(a)),
                                     look_at, position, angle,
                                     rel_desired_direction) <
           DirectionSimilarityRating(library.Get(static_cast<uint32_t>(b)),
                                     look_at, position, angle,
                                     rel_desired_direction);
  });
}

// Movement-similarity sort with an explicit cornering bias.
inline void SortByMovementSimilarity(const AnimationLibrary& library,
                                     std::vector<int>& data_set,
                                     const Vector3& rel_desired_direction,
                                     e_Velocity desired_velocity,
                                     float cornering_bias) {
  std::stable_sort(data_set.begin(), data_set.end(), [&](int a, int b) {
    return MovementSimilarity(library.Get(static_cast<uint32_t>(a)),
                              rel_desired_direction, desired_velocity,
                              cornering_bias) <
           MovementSimilarity(library.Get(static_cast<uint32_t>(b)),
                              rel_desired_direction, desired_velocity,
                              cornering_bias);
  });
}

// ANIM-2 A10.3b: the legacy `useDesiredMovement == false` chain. No direction
// filter runs (the legacy chain applied that only when the command carried
// desired movement), but everything else matches order-for-order. When
// `without_foot_out` is non-null it receives the same chain minus the foot
// sort, for the foot counterfactual diagnostic.
inline std::vector<int> RefineLegacyCandidates(
    const AnimationLibrary& library, std::span<const int> crude,
    const MovementRefinementRequest& request, e_FunctionType function_type,
    bool use_desired_trip_direction, const Vector3& rel_desired_trip_direction,
    std::vector<int>* without_foot_out = nullptr) {
  std::vector<int> data_set(crude.begin(), crude.end());
  if (data_set.empty()) {
    if (without_foot_out != nullptr) without_foot_out->clear();
    return data_set;
  }

  SortByPriority(library, data_set);
  SortByIdle(library, data_set, request.idle_level);
  if (without_foot_out != nullptr) *without_foot_out = data_set;
  SortByFoot(library, data_set, request.desired_foot);
  if (!request.is_ball_control) {
    SortByIncomingBodyDirection(library, data_set, request.rel_incoming_body_direction);
  }
  SortByIncomingVelocity(library, data_set, request.incoming_velocity);
  if (use_desired_trip_direction) {
    SortByTripDirection(library, data_set, rel_desired_trip_direction);
  }
  if (function_type != e_FunctionType_Movement) SortByBaseanim(library, data_set);
  if (function_type == e_FunctionType_Deflect) SortByCatchOrDeflect(library, data_set);

  if (without_foot_out != nullptr) {
    if (!request.is_ball_control) {
      SortByIncomingBodyDirection(library, *without_foot_out,
                                  request.rel_incoming_body_direction);
    }
    SortByIncomingVelocity(library, *without_foot_out, request.incoming_velocity);
    if (use_desired_trip_direction) {
      SortByTripDirection(library, *without_foot_out, rel_desired_trip_direction);
    }
    if (function_type != e_FunctionType_Movement) {
      SortByBaseanim(library, *without_foot_out);
    }
    if (function_type == e_FunctionType_Deflect) {
      SortByCatchOrDeflect(library, *without_foot_out);
    }
  }
  return data_set;
}

// ANIM-2 A10.3c: the idle Movement clip, relocated from
// Player::GetIdleMovementAnimID. Pure over the library and the pose facts.
inline AnimationId SelectIdleMovementAnim(const AnimationLibrary& library,
                                          const Vector3& position, radian angle,
                                          float float_velocity) {
  CrudeSelectionQuery query;
  query.byFunctionType = true;
  query.functionType = e_FunctionType_Movement;
  query.byIncomingVelocity = true;
  query.incomingVelocity = e_Velocity_Idle;
  query.byOutgoingVelocity = true;
  query.outgoingVelocity = e_Velocity_Idle;

  DataSet data_set;
  BakedAnimationSelector::CrudeSelection(library.Clips(), query, data_set);

  SortByIdle(library, data_set, 1.0f);
  SortByIncomingBodyDirection(library, data_set, Vector3(0, -1, 0));
  SortByIncomingVelocity(library, data_set, e_Velocity_Idle);

  const Vector3 rel_desired = Vector3(0, -1, 0);
  const Vector3 desired_movement = rel_desired * EnumToFloatVelocity(e_Velocity_Idle);
  const radian cornering_bias = football::animation::CorneringBiasForFastCornering(
      Vector3(0, -1.0f * float_velocity, 0), desired_movement, 1.0f, 0.9f);
  SortByBodyDirectionSimilarity(library, data_set,
                                position + Vector3(0, -10, 0).GetRotated2D(angle),
                                position, angle, rel_desired);
  SortByMovementSimilarity(library, data_set, rel_desired, e_Velocity_Idle,
                           cornering_bias);
  return static_cast<AnimationId>(data_set.front());
}

// ANIM-2 A10.3c: the retain-animation clip, relocated from
// Player::SelectRetainAnim. It sorts by the legacy movement similarity with
// the caller's predicate values, exactly as the Player used to.
inline AnimationId SelectRetainAnimClip(const AnimationLibrary& library,
                                        const Vector3& rel_desired_direction,
                                        e_Velocity desired_velocity,
                                        float cornering_bias) {
  CrudeSelectionQuery query;
  query.byFunctionType = true;
  query.functionType = e_FunctionType_Movement;
  query.byIncomingVelocity = true;
  query.incomingVelocity = e_Velocity_Idle;
  query.byOutgoingVelocity = true;
  query.outgoingVelocity = e_Velocity_Idle;
  query.properties.incoming_retain_state = "right_elbow";

  DataSet data_set;
  BakedAnimationSelector::CrudeSelection(library.Clips(), query, data_set);
  assert(!data_set.empty());
  SortByMovementSimilarity(library, data_set, rel_desired_direction,
                           desired_velocity, cornering_bias);
  return static_cast<AnimationId>(data_set.front());
}

#endif  // FOOTBALL_ANIMATION_MOVEMENT_SELECTION_HPP
