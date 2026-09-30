#ifndef FOOTBALL_CORE_PHYSICS_CONTACT_BALL_WOODWORK_CONTACT_HPP
#define FOOTBALL_CORE_PHYSICS_CONTACT_BALL_WOODWORK_CONTACT_HPP

#include <optional>

#include "core/model/ball/ball.hpp"
#include "core/physics/contact/ball/ball_contact.hpp"
#include "core/physics/contact/ball/goal_geometry.hpp"
#include "core/physics/contact/geometry/capsule_collider.hpp"
#include "core/physics/contact/geometry/sphere_capsule_contact.hpp"
#include "core/physics/contact/geometry/sweep_sphere_capsule.hpp"

namespace football_sim::contact {

// Posts are vertical capsules at the four goal-mouth corners and the crossbar
// is a horizontal capsule along each goal line.
inline std::optional<BallContact> DetectBallWoodworkContact(
    const football_sim::Ball& ball, const GoalGeometry& geometry) {
  const football_sim::BallState& state = ball.State();
  const float postX[2] = {-geometry.halfWidth, geometry.halfWidth};
  const float postY[2] = {-geometry.goalHalfWidth, geometry.goalHalfWidth};
  for (float x : postX) {
    for (float y : postY) {
      CapsuleCollider post;
      post.tipA = football_sim::math::Vector3(x, y, 0.0f);
      post.tipB = football_sim::math::Vector3(x, y, geometry.goalHeight);
      post.radius = geometry.postRadius;
      if (auto contact =
              DetectSphereCapsuleContact(state.position, ball.Radius(), post)) {
        return contact;
      }
    }
  }

  const float barX[2] = {-geometry.halfWidth, geometry.halfWidth};
  for (float x : barX) {
    CapsuleCollider bar;
    bar.tipA =
        football_sim::math::Vector3(x, -geometry.goalHalfWidth, geometry.goalHeight);
    bar.tipB =
        football_sim::math::Vector3(x, geometry.goalHalfWidth, geometry.goalHeight);
    bar.radius = geometry.postRadius;
    if (auto contact =
            DetectSphereCapsuleContact(state.position, ball.Radius(), bar)) {
      return contact;
    }
  }
  return std::nullopt;
}

// Swept woodwork contact: the sphere centre moves along the chord start -> end;
// returns the earliest timeWithinTick across posts and crossbar.
inline std::optional<BallContact> SweepBallWoodworkContact(
    const football_sim::math::Vector3& start,
    const football_sim::math::Vector3& end, const football_sim::Ball& ball,
    const GoalGeometry& geometry, float dt) {
  std::optional<BallContact> earliest;
  const auto consider = [&](const CapsuleCollider& capsule) {
    if (auto contact =
            SweepSphereCapsule(start, end, ball.Radius(), capsule, dt)) {
      if (!earliest || contact->timeWithinTick < earliest->timeWithinTick) {
        earliest = contact;
      }
    }
  };

  const float postX[2] = {-geometry.halfWidth, geometry.halfWidth};
  const float postY[2] = {-geometry.goalHalfWidth, geometry.goalHalfWidth};
  for (float x : postX) {
    for (float y : postY) {
      CapsuleCollider post;
      post.tipA = football_sim::math::Vector3(x, y, 0.0f);
      post.tipB = football_sim::math::Vector3(x, y, geometry.goalHeight);
      post.radius = geometry.postRadius;
      consider(post);
    }
  }
  const float barX[2] = {-geometry.halfWidth, geometry.halfWidth};
  for (float x : barX) {
    CapsuleCollider bar;
    bar.tipA =
        football_sim::math::Vector3(x, -geometry.goalHalfWidth, geometry.goalHeight);
    bar.tipB =
        football_sim::math::Vector3(x, geometry.goalHalfWidth, geometry.goalHeight);
    bar.radius = geometry.postRadius;
    consider(bar);
  }
  return earliest;
}

}  // namespace football_sim::contact

#endif  // FOOTBALL_CORE_PHYSICS_CONTACT_BALL_WOODWORK_CONTACT_HPP
