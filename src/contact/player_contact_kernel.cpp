#include "football/contact/player_contact_kernel.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

#include "football/animation/types.hpp"
#include "football/foundation/math/scalar.hpp"

namespace football::sim::player {
namespace {

using football::math::clamp;
using football::math::NormalizedClamp;
using football::math::Vector3;

constexpr float kDistanceFactor = 0.72f;
constexpr float kBouncePlayerRadius = 0.5f * kDistanceFactor;
constexpr float kSimilarPlayerRadius = 0.8f * kDistanceFactor;
constexpr float kSimilarExp = 0.2f;
constexpr float kSimilarForceFactor = 0.25f;

void ApplyLocalOffset(ContactActorSnapshot& actor, const Vector3& offset) {
  actor.position += offset;
  actor.collider.center += offset;
  actor.decaying_position_offset += offset;
  if (actor.decaying_position_offset.GetLength() > 0.1f)
    actor.decaying_position_offset =
        actor.decaying_position_offset.GetNormalized() * 0.1f;
}

}  // namespace

ContactPairResult ResolveContactPair(const ContactActorSnapshot& first_input,
                                     const ContactActorSnapshot& second_input,
                                     const ContactPairEnvironment& environment) {
  ContactPairResult result;
  ContactActorSnapshot p1 = first_input;
  ContactActorSnapshot p2 = second_input;

  Vector3 p1pos = p1.collider.center;
  Vector3 p2pos = p2.collider.center;
  const float distance = (p1pos - p2pos).GetLength();
  result.distance = distance;

  assert(p1.velocity.coords[2] == 0.0f);
  assert(p2.velocity.coords[2] == 0.0f);

  float p1backFacing = 0.5f;
  float p2backFacing = 0.5f;
  Vector3 bounceVec;

  if (distance < kBouncePlayerRadius * 2.0f ||
      distance < (kBouncePlayerRadius + kSimilarPlayerRadius) * 2.0f) {
    bounceVec = (p1pos - p2pos).GetNormalized(Vector3(0, -1, 0));

    const Vector3 p1facing =
        p1.facing.GetRotated2D(p1.relative_body_angle * 0.7f);
    const Vector3 p2facing =
        p2.facing.GetRotated2D(p2.relative_body_angle * 0.7f);
    p1backFacing =
        clamp(p1facing.GetDotProduct(bounceVec) * 0.5f + 0.5f, 0.0f, 1.0f);
    p2backFacing =
        clamp(p2facing.GetDotProduct(-bounceVec) * 0.5f + 0.5f, 0.0f, 1.0f);

    if (p1.collider.Intersects(p2.collider)) {
      float bounceBias = 0.0f;
      bounceBias += p1backFacing * 0.8f;
      bounceBias -= p2backFacing * 0.8f;

      bounceBias -= clamp(
          ((p1.float_velocity - p2.float_velocity) / sprintVelocity) * 0.2f,
          -0.2f, 0.2f);

      if (p1.action.IsContactPending() &&
          p1.action.type == e_FunctionType_Interfere)
        bounceBias += 0.1f + 0.4f * p1.technical_standingtackle;
      if (p1.action.IsContactPending() &&
          p1.action.type == e_FunctionType_Sliding)
        bounceBias += 0.1f + 0.4f * p1.technical_slidingtackle;
      if (p2.action.IsContactPending() &&
          p2.action.type == e_FunctionType_Interfere)
        bounceBias -= 0.1f + 0.4f * p2.technical_standingtackle;
      if (p2.action.IsContactPending() &&
          p2.action.type == e_FunctionType_Sliding)
        bounceBias -= 0.1f + 0.4f * p2.technical_slidingtackle;

      if (p1.global_designated_possession) bounceBias += 0.4f;
      if (p2.global_designated_possession) bounceBias -= 0.4f;

      if (p1.team_designated_possession && p2.team_designated_possession) {
        const float p1_ball_distance =
            (environment.ball_next_position - p1.position).GetLength();
        const float p2_ball_distance =
            (environment.ball_next_position - p2.position).GetLength();
        bounceBias += clamp(std::min(p2_ball_distance, 1.2f) -
                                std::min(p1_ball_distance, 1.2f),
                            -0.6f, 0.6f);
      }

      bounceBias += p1.physical_balance;
      bounceBias -= p2.physical_balance;

      bounceBias = clamp(bounceBias, -1.0f, 1.0f);
      bounceBias *= 0.5f;
      const float bounceBias0to1 = bounceBias * 0.5f + 0.5f;

      Vector3 offset1 = (p1pos - p2pos).GetNormalized(0) *
                        (kBouncePlayerRadius - distance * 0.5f) *
                        (1.0f - bounceBias0to1) * 2.0f;
      Vector3 offset2 = (p2pos - p1pos).GetNormalized(0) *
                        (kBouncePlayerRadius - distance * 0.5f) *
                        bounceBias0to1 * 2.0f;

      if (p2.global_designated_possession && p2.has_possession) {
        const Vector3 p2_left =
            p2pos + p2.facing.GetRotated2D(0.3f * pi) *
                        kBouncePlayerRadius * 2;
        const Vector3 p2_right =
            p2pos + p2.facing.GetRotated2D(-0.3f * pi) *
                        kBouncePlayerRadius * 2;
        const Vector3 p2side = (p1pos - p2_left).GetLength() <
                                       (p1pos - p2_right).GetLength()
                                   ? p2_left
                                   : p2_right;
        offset1 += (p2side - p1pos).GetNormalizedMax(0.01f) *
                   p1.physical_balance * 0.3f;
      } else if (p1.global_designated_possession && p1.has_possession) {
        const Vector3 p1_left =
            p1pos + p1.facing.GetRotated2D(0.3f * pi) *
                        kBouncePlayerRadius * 2;
        const Vector3 p1_right =
            p1pos + p1.facing.GetRotated2D(-0.3f * pi) *
                        kBouncePlayerRadius * 2;
        const Vector3 p1side = (p2pos - p1_left).GetLength() <
                                       (p2pos - p1_right).GetLength()
                                   ? p1_left
                                   : p1_right;
        offset2 += (p1side - p2pos).GetNormalizedMax(0.01f) *
                   p2.physical_balance * 0.3f;
      }

      offset1.NormalizeMax(sprintVelocity * 0.01f);
      offset2.NormalizeMax(sprintVelocity * 0.01f);

      result.first_has_offset = true;
      result.second_has_offset = true;
      result.first_offset = offset1;
      result.second_offset = offset2;
      ApplyLocalOffset(p1, offset1);
      ApplyLocalOffset(p2, offset2);
    }

    float similarBias = 0.0f;
    if (kSimilarForceFactor > 0.0f &&
        distance < (kBouncePlayerRadius + kSimilarPlayerRadius) * 2.0f) {
      const float shellDistance =
          std::max(0.0f, distance - kBouncePlayerRadius * 2.0f);

      similarBias += p1backFacing * 0.8f;
      similarBias -= p2backFacing * 0.8f;
      similarBias -= clamp(
          ((p1.float_velocity - p2.float_velocity) / sprintVelocity) * 0.2f,
          -0.2f, 0.2f);

      if (p1.global_designated_possession) similarBias += 0.6f;
      if (p2.global_designated_possession) similarBias -= 0.6f;

      if (p1.team_designated_possession && p2.team_designated_possession) {
        const float p1_ball_distance =
            (environment.ball_next_position - p1.position).GetLength();
        const float p2_ball_distance =
            (environment.ball_next_position - p2.position).GetLength();
        similarBias += clamp(std::min(p2_ball_distance, 1.2f) -
                                 std::min(p1_ball_distance, 1.2f),
                             -0.6f, 0.6f);
      }

      similarBias += p1.physical_balance;
      similarBias -= p2.physical_balance;
      similarBias = clamp(similarBias, -1.0f, 1.0f);
      similarBias *= 0.9f;

      float similarForce =
          clamp(1.0f - (shellDistance / (kSimilarPlayerRadius * 2.0f)),
                0.0f, 1.0f);
      similarForce = std::pow(similarForce, kSimilarExp);
      similarForce *= kSimilarForceFactor;

      const float similarBias0to1 = similarBias * 0.5f + 0.5f;
      result.first_similar = true;
      result.second_similar = true;
      result.first_similar_force = similarForce * (1.0f - similarBias0to1);
      result.second_similar_force = similarForce * similarBias0to1;
    }

    if (distance < kBouncePlayerRadius * 2.0f) {
      result.trip_range = true;
      float p1sensitivity = 0.0f;
      float p2sensitivity = 0.0f;

      p1sensitivity += (1.0f - p1backFacing);
      p2sensitivity += (1.0f - p2backFacing);

      p1sensitivity +=
          NormalizedClamp(p1.float_velocity, idleVelocity, sprintVelocity);
      p2sensitivity +=
          NormalizedClamp(p2.float_velocity, idleVelocity, sprintVelocity);

      if (p1.has_best_possession) p1sensitivity += 1.0f;
      if (p2.has_best_possession) p2sensitivity += 1.0f;

      constexpr float balanceWeight = 3.0f;
      p1sensitivity += (1.0f - p1.physical_balance) * balanceWeight;
      p2sensitivity += (1.0f - p2.physical_balance) * balanceWeight;

      p1sensitivity += clamp(
          p1.decaying_position_offset.GetLength() * 10.0f, 0.0f, 1.0f);
      p2sensitivity += clamp(
          p2.decaying_position_offset.GetLength() * 10.0f, 0.0f, 1.0f);

      constexpr float penetrationWeight = 6.0f;
      const float penetration =
          ((p1.position + p1.velocity * 0.03f) -
           (p2.position + p2.velocity * 0.03f))
              .GetLength();
      const float penetrationBonus =
          std::pow(1.0f - NormalizedClamp(
                              penetration, 0.0f, kBouncePlayerRadius * 2.0f),
                   0.4f) *
          penetrationWeight;
      p1sensitivity += penetrationBonus;
      p2sensitivity += penetrationBonus;

      const float p1_ball_distance =
          (environment.ball_next_position - p1.position).GetLength();
      const float p2_ball_distance =
          (environment.ball_next_position - p2.position).GetLength();
      p1sensitivity +=
          1.0f - NormalizedClamp(p1_ball_distance, 0.0f, 0.7f);
      p2sensitivity +=
          1.0f - NormalizedClamp(p2_ball_distance, 0.0f, 0.7f);

      p1sensitivity /= 5.0f + balanceWeight + penetrationWeight;
      p2sensitivity /= 5.0f + balanceWeight + penetrationWeight;
      result.first_sensitivity = p1sensitivity;
      result.second_sensitivity = p2sensitivity;

      constexpr float trip0threshold = 0.38f;
      constexpr float trip1threshold = 0.48f;
      constexpr float trip2threshold = 0.58f;

      if (p1sensitivity > trip0threshold) {
        int tripType = 0;
        if (p1sensitivity > trip1threshold) tripType = 1;
        if (p1sensitivity > trip2threshold) tripType = 2;
        if (tripType > 0) {
          result.first_trip.active = true;
          result.first_trip.severity = p1sensitivity;
          result.first_trip.trip_type = tripType;
          result.first_trip.direction =
              (p1.velocity * 0.1f + p2.velocity * 0.06f + bounceVec * 1.0f)
                  .GetNormalized(bounceVec);
        }
      }
      if (p2sensitivity > trip0threshold) {
        int tripType = 0;
        if (p2sensitivity > trip1threshold) tripType = 1;
        if (p2sensitivity > trip2threshold) tripType = 2;
        if (tripType > 0) {
          result.second_trip.active = true;
          result.second_trip.severity = p2sensitivity;
          result.second_trip.trip_type = tripType;
          result.second_trip.direction =
              (p2.velocity * 0.1f + p1.velocity * 0.06f - bounceVec * 1.0f)
                  .GetNormalized(-bounceVec);
        }
      }
    }
  }

  return result;
}

}  // namespace football::sim::player
