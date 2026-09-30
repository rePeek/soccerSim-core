// Copyright 2019 Google LLC & Bastiaan Konings
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "animation/baked_selector.hpp"

#include <cmath>
#include <string>

#include "foundation/utils.hpp"

using namespace blunted;

namespace {

// Mirrors legacy AnimCollection::_CheckFunctionType.
bool CheckFunctionType(int32_t anim_type, e_FunctionType query_type) {
  switch (query_type) {
    case e_FunctionType_Movement:
      return anim_type == e_DefString_Movement;
    case e_FunctionType_BallControl:
      return anim_type == e_DefString_BallControl;
    case e_FunctionType_Trap:
      return anim_type == e_DefString_Trap;
    case e_FunctionType_ShortPass:
      return anim_type == e_DefString_ShortPass;
    case e_FunctionType_LongPass:
      return anim_type == e_DefString_LongPass;
    case e_FunctionType_HighPass:
      return anim_type == e_DefString_HighPass;
    case e_FunctionType_Shot:
      return anim_type == e_DefString_Shot;
    case e_FunctionType_Deflect:
      return anim_type == e_DefString_Deflect;
    case e_FunctionType_Catch:
      return anim_type == e_DefString_Catch;
    case e_FunctionType_Interfere:
      return anim_type == e_DefString_Interfere;
    case e_FunctionType_Trip:
      return anim_type == e_DefString_Trip;
    case e_FunctionType_Sliding:
      return anim_type == e_DefString_Sliding;
    case e_FunctionType_Special:
      return anim_type == e_DefString_Special;
    default:
      return false;
  }
}

// Legacy AnimCollection constructor default deviations.
constexpr float kMaxIncomingBallDirectionDeviation = 0.25f * pi;
constexpr float kMaxOutgoingBallDirectionDeviation = 0.25f * pi;

}  // namespace

void BakedAnimationSelector::CrudeSelection(
    const std::vector<AnimationClip>& clips,
    const CrudeSelectionQuery& query,
    DataSet& data_set) {
  const int anim_size = static_cast<int>(clips.size());

  for (int i = 0; i < anim_size; i++) {
    const AnimationClip& clip = clips[i];
    const FootballAnimationMetadata& m = clip.metadata;
    const int32_t anim_type = m.action_type;

    // select by TYPE
    if (query.byFunctionType) {
      if (!CheckFunctionType(anim_type, query.functionType)) continue;
    }

    // select by INCOMING VELOCITY
    if (query.byIncomingVelocity) {
      const e_Velocity anim_incoming_velocity =
          FloatToEnumVelocity(m.incoming_velocity);

      if (!query.incomingVelocity_Strict) {
        if (query.incomingVelocity_NoDribbleToIdle) {
          if (anim_incoming_velocity == e_Velocity_Idle &&
              query.incomingVelocity == e_Velocity_Dribble)
            continue;
        }
        if (anim_incoming_velocity == e_Velocity_Idle &&
            query.incomingVelocity == e_Velocity_Walk)
          continue;
        if (anim_incoming_velocity == e_Velocity_Idle &&
            query.incomingVelocity == e_Velocity_Sprint)
          continue;
        if (anim_incoming_velocity == e_Velocity_Dribble &&
            query.incomingVelocity == e_Velocity_Idle)
          continue;
        if (anim_incoming_velocity == e_Velocity_Walk &&
            query.incomingVelocity == e_Velocity_Idle)
          continue;
        if (anim_incoming_velocity == e_Velocity_Sprint &&
            query.incomingVelocity == e_Velocity_Idle)
          continue;

        if (query.incomingVelocity_NoDribbleToSprint) {
          if (anim_incoming_velocity == e_Velocity_Sprint &&
              query.incomingVelocity == e_Velocity_Dribble)
            continue;
        }

        if (query.incomingVelocity_ForceLinearity) {
          float anim_incoming_velocity_float = RangeVelocity(m.incoming_velocity);
          float anim_outgoing_velocity_float = RangeVelocity(m.outgoing_velocity);
          float query_velocity_float = EnumToFloatVelocity(query.incomingVelocity);

          if (FloatToEnumVelocity(anim_incoming_velocity_float) ==
              e_Velocity_Dribble)
            anim_incoming_velocity_float = walkVelocity;
          if (FloatToEnumVelocity(anim_outgoing_velocity_float) ==
              e_Velocity_Dribble)
            anim_outgoing_velocity_float = walkVelocity;
          if (FloatToEnumVelocity(query_velocity_float) == e_Velocity_Dribble)
            query_velocity_float = walkVelocity;

          if (anim_incoming_velocity_float >
              std::max(query_velocity_float, anim_outgoing_velocity_float))
            continue;
          if (anim_incoming_velocity_float <
              std::min(query_velocity_float, anim_outgoing_velocity_float))
            continue;
        }
      } else {
        if (anim_incoming_velocity != query.incomingVelocity) continue;
      }
    }

    // select by OUTGOING VELOCITY
    if (query.byOutgoingVelocity) {
      if (FloatToEnumVelocity(m.outgoing_velocity) != query.outgoingVelocity)
        continue;
    }

    // CULL WRONG ROTATIONAL SIDE
    if (query.bySide) {
      Vector3 anim_incoming_direction = m.incoming_body_direction;
      Vector3 anim_outgoing_direction =
          m.outgoing_direction.GetRotated2D(m.outgoing_body_angle);
      radian anim_turn_angle =
          anim_outgoing_direction.GetAngle2D(anim_incoming_direction);

      Vector3 fenced_direction = query.lookAtVecRel.GetRotated2D(pi);

      if (std::fabs(anim_turn_angle) > 0.06f * pi) {
        e_Side anim_side = (anim_turn_angle > 0) ? e_Side_Left : e_Side_Right;

        radian anim_incoming_to_fence_angle =
            fenced_direction.GetAngle2D(anim_incoming_direction);
        radian query_incoming_to_fence_angle =
            fenced_direction.GetAngle2D(query.incomingBodyDirection);
        radian fence_to_outgoing_angle =
            anim_outgoing_direction.GetAngle2D(fenced_direction);

        e_Side anim_incoming_to_fence_side =
            (anim_incoming_to_fence_angle > 0) ? e_Side_Left : e_Side_Right;
        e_Side query_incoming_to_fence_side =
            (query_incoming_to_fence_angle > 0) ? e_Side_Left : e_Side_Right;
        e_Side fence_to_anim_outgoing_side =
            (fence_to_outgoing_angle > 0) ? e_Side_Left : e_Side_Right;

        if (anim_incoming_to_fence_side == anim_side &&
            fence_to_anim_outgoing_side == anim_side &&
            std::fabs(anim_incoming_to_fence_angle + fence_to_outgoing_angle) <
                pi)
          continue;
        if (query_incoming_to_fence_side == anim_side &&
            fence_to_anim_outgoing_side == anim_side &&
            std::fabs(query_incoming_to_fence_side + fence_to_outgoing_angle) <
                pi)
          continue;
      }
    }

    // select by RETAIN BALL
    if (query.byPickupBall) {
      if ((m.outgoing_retain_state.empty() && query.pickupBall) ||
          (!m.outgoing_retain_state.empty() && !query.pickupBall)) {
        continue;
      }
    }

    // select LAST DITCH ANIMS
    if (!query.allowLastDitchAnims) {
      if (m.last_ditch) continue;
    }

    // select by INCOMING BODY ANGLE
    if (query.byIncomingBodyDirection &&
        !(query.byIncomingVelocity &&
          query.incomingVelocity == e_Velocity_Idle)) {
      const radian margin_radians = 0.06f * pi;

      if (FloatToEnumVelocity(m.incoming_velocity) != e_Velocity_Idle) {
        Vector3 incoming_body_dir = m.incoming_body_direction;

        if (std::fabs(FixAngle(incoming_body_dir.GetAngle2D())) >
            std::fabs(FixAngle(query.incomingBodyDirection.GetAngle2D())) +
                margin_radians)
          continue;

        Vector3 outgoing_body_dir =
            Vector3(0, -1, 0).GetRotated2D(m.outgoing_body_angle +
                                           m.outgoing_angle);

        if (query.incomingBodyDirection_Strict) {
          if (std::fabs(incoming_body_dir.GetAngle2D(query.incomingBodyDirection)) >
              margin_radians)
            continue;
        } else {
          if (std::fabs(incoming_body_dir.GetAngle2D(query.incomingBodyDirection)) >
              0.5f * pi + margin_radians)
            continue;
        }

        if (query.incomingBodyDirection_ForceLinearity) {
          radian shortest_angle_1 = incoming_body_dir.GetAngle2D(outgoing_body_dir);
          radian shortest_angle_2 =
              incoming_body_dir.GetAngle2D(query.incomingBodyDirection);
          if ((shortest_angle_1 > margin_radians &&
               shortest_angle_2 > margin_radians) ||
              (shortest_angle_1 < -margin_radians &&
               shortest_angle_2 < -margin_radians)) {
            continue;
          }
          if (std::fabs(shortest_angle_1) + std::fabs(shortest_angle_2) >
              pi + margin_radians)
            continue;
        }
      } else {
        if (query.incomingBodyDirection_Strict) {
          if (std::fabs(Vector3(0, -1, 0).GetAngle2D(query.incomingBodyDirection)) >
              margin_radians)
            continue;
        } else {
          if (std::fabs(Vector3(0, -1, 0).GetAngle2D(query.incomingBodyDirection)) >
              0.25f * pi + margin_radians)
            continue;
        }
      }
    }

    // select by INCOMING BALL DIRECTION
    if (query.byIncomingBallDirection) {
      Vector3 anim_ball_direction = m.incoming_ball_direction;
      if (anim_ball_direction.GetLength() < 0.1f) {
        Log(e_FatalError, "BakedAnimationSelector", "CrudeSelection",
            "Anim " + clip.name + " missing incoming ball direction");
      }
      if (anim_ball_direction.GetLength() != 0.0f &&
          query.incomingBallDirection.GetLength() != 0.0f) {
        anim_ball_direction.coords[2] *= 0.4f;
        anim_ball_direction.Normalize();
        Vector3 adapted_incoming_ball_direction = query.incomingBallDirection;
        adapted_incoming_ball_direction.coords[2] *= 0.4f;
        adapted_incoming_ball_direction.Normalize();

        radian ball_direction_angle =
            std::fabs(adapted_incoming_ball_direction.GetAngle2D(
                anim_ball_direction));
        radian max_deviation =
            std::fabs(m.incoming_ball_direction_max_deviation * pi);
        if (max_deviation == 0.0f) {
          max_deviation = kMaxIncomingBallDirectionDeviation;
          if (anim_type == e_DefString_Deflect) max_deviation = 0.4f * pi;
        }
        if (ball_direction_angle > max_deviation) continue;
      }
    }

    // select by OUTGOING BALL DIRECTION
    if (query.byOutgoingBallDirection) {
      Vector3 anim_ball_direction = m.outgoing_ball_direction;
      anim_ball_direction.Normalize(Vector3(0));
      radian ball_direction_angle =
          std::fabs(query.outgoingBallDirection.Get2D()
                        .GetNormalized(anim_ball_direction)
                        .GetAngle2D(anim_ball_direction));
      radian max_deviation =
          std::fabs(m.outgoing_ball_direction_max_deviation * pi);
      if (max_deviation == 0.0) {
        max_deviation = kMaxOutgoingBallDirectionDeviation;
      }
      if (ball_direction_angle > max_deviation) continue;
    }

    // select by PROPERTIES
    if (query.properties.incoming_special_state() != m.incoming_special_state)
      continue;
    if ((query.functionType == e_FunctionType_Deflect ||
         ((query.properties.incoming_retain_state().empty()) !=
          m.incoming_retain_state.empty())) &&
        query.properties.incoming_retain_state() != m.incoming_retain_state)
      continue;
    if (query.properties.specialvar1() != m.special_var1) continue;
    if (query.properties.specialvar2() != m.special_var2) continue;

    // select by TRIP TYPE
    if (query.byTripType) {
      if (m.trip_type != query.tripType) continue;
    }

    // select by FORCED FOOT
    if (query.heedForcedFoot) {
      int which = 0;
      if (m.forced_foot == "strong") which = 1;
      else if (m.forced_foot == "weak") which = 2;
      if (which != 0) {
        e_Foot anim_foot = e_Foot_Right;
        if (m.touch_foot == "left") anim_foot = e_Foot_Left;

        if (m.current_foot == e_Foot_Left) {
          if (anim_foot == e_Foot_Left) anim_foot = e_Foot_Right;
          else anim_foot = e_Foot_Left;
        }

        if (which == 1 && query.strongFoot != anim_foot) continue;
        if (which == 2 && query.strongFoot == anim_foot) continue;
      }
    }

    data_set.push_back(static_cast<int>(clip.id));
  }
}

Quadrant BakedAnimationSelector::GetQuadrant(int id) {
  Quadrant q;
  q.id = id;
  if (id == 0) {
    q.velocity = e_Velocity_Idle;
    q.angle = 0;
    q.position = Vector3(0, 0, 0);
    return q;
  }

  const int rel = id - 1;
  const int velocity_id = rel / 11;
  const int angle_id = rel % 11;
  if (velocity_id == 0) q.velocity = e_Velocity_Dribble;
  else if (velocity_id == 1) q.velocity = e_Velocity_Walk;
  else q.velocity = e_Velocity_Sprint;

  // Angle table mirrors legacy AnimCollection. angle_id 10 was an
  // uninitialized entry in the legacy constructor and is never assigned to
  // any clip; use 0 so the lookup stays deterministic.
  const float angle_table[11] = {
      pi / 180.0f * 0.0f,   pi / 180.0f * 20.0f,  pi / 180.0f * 45.0f,
      pi / 180.0f * 90.0f,  pi / 180.0f * 135.0f, pi / 180.0f * 179.0f,
      pi / 180.0f * -20.0f, pi / 180.0f * -45.0f, pi / 180.0f * -90.0f,
      pi / 180.0f * -135.0f, 0.0f};
  q.angle = angle_table[angle_id];
  q.position = Vector3(0, -1, 0).GetRotated2D(q.angle) *
               EnumToFloatVelocity(q.velocity);
  return q;
}