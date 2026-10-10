#include <algorithm>
#include <cmath>
#include <limits>
#include <catch2/catch_test_macros.hpp>
#include "football/ball/ball.hpp"
#include "sim/player/player_body_collision_shadow.hpp"
#include "sim/player/player_body_pose_shadow.hpp"
#include "sim/player/player_action_executor.hpp"

namespace {
using namespace football::ball;
using blunted::Vector3;
using football::sim::TickSpan;
using Motions = std::array<ColliderMotion, kPlayerBodyPartCount>;
Motions Body(e_FunctionType action, PlayerKinematicState start = {}, std::uint32_t slot = 0,
             Vector3 axis = {1, 0, 0}) {
  auto end = PredictBodyKinematicState(start, TickSpan{1});
  Motions motions;
  BuildShadowPosedBodyMotions(start, end, action, axis, PlayerBodyColliderIdsForSlot(slot), motions);
  return motions;
}
BallState State(Vector3 position, Vector3 velocity) {
  return {position, velocity, Vector3(0), blunted::Quaternion{}};
}
BallStepResult Step(const BallState& state, std::span<const ColliderMotion> motions) {
  Ball ball{football::model::Pitch{}};
  ball.Reset(state);
  return ball.Step(BallTickInput{motions, {}});
}
void Near(const Vector3& a, const Vector3& b) { REQUIRE((a - b).GetLength() < 2e-5f); }
void Mirror(ColliderMotion& collider) {
  const auto turn = [](ColliderShape& shape) {
    std::visit([](auto& s) {
      using T = std::decay_t<decltype(s)>;
      if constexpr (std::is_same_v<T, Sphere>) s.center.Mirror();
      else if constexpr (std::is_same_v<T, Capsule>) { s.a.Mirror(); s.b.Mirror(); }
      else { s.point.Mirror(); s.normal.Mirror(); }
    }, shape);
  };
  turn(collider.start); turn(collider.end);
}
}

TEST_CASE("episodes separate geometry impacts and touch windows without cooldown", "[sim][body-episodes]") {
  BodyContactEpisodes geometry, impacts;
  geometry.Resize(6); impacts.Resize(6);
  std::uint64_t runs = 0, impact_runs = 0;
  for (std::uint64_t step = 1; step <= 5; ++step) {
    const auto g = geometry.Observe(1, step, 0, step == 1);
    runs += !g.continued;
    REQUIRE(g.length == step);
    // Only ticks 1 and 4 impact; resting/overlap is not an impact episode.
    if (step == 1 || step == 4) impact_runs += !impacts.Observe(1, step, 0, false).continued;
    geometry.Complete(step, 0); impacts.Complete(step, 0);
  }
  REQUIRE(runs == 1); REQUIRE(impact_runs == 2);
  REQUIRE_FALSE(geometry.Observe(1, 6, 0, true).continued); // known separation
  REQUIRE_FALSE(geometry.Observe(1, 8, 0, false).continued); // missing tick
  REQUIRE_FALSE(geometry.Observe(1, 9, 1, false).continued); // reset/teleport
  REQUIRE_FALSE(geometry.Observe(2, 10, 1, false).continued); // another part
  geometry.Complete(10, 1);
  REQUIRE_FALSE(geometry.Observe(1, 11, 1, false).continued); // absent last frame
  geometry.Break();
  REQUIRE_FALSE(geometry.Observe(1, 12, 1, false).continued); // end change/disabled
  geometry.Observe(0, std::numeric_limits<std::uint64_t>::max(), 1, false);
  REQUIRE_FALSE(geometry.Observe(0, 0, 1, false).continued); // no overflow bridging
}

TEST_CASE("fixed standing running sliding and fallen poses use distinct occupied space", "[sim][body-poses]") {
  const auto upright = Body(e_FunctionType_Movement);
  const auto hit = Step(State({-.4f, 0, .6f}, {80, 0, 0}), upright);
  REQUIRE(hit.contacts.front().collider == upright[1].id);
  REQUIRE(hit.contacts.front().normal_impulse > 0);
  REQUIRE(hit.state.velocity.coords[0] < 0);
  PlayerKinematicState runner; runner.velocity = {-4, 0, 0};
  const auto running = Body(e_FunctionType_Movement, runner);
  const auto moving = Step(State({-.4f, 0, .6f}, {80, 0, 0}), running);
  REQUIRE(moving.contacts.front().toi < hit.contacts.front().toi);
  REQUIRE(std::fabs(moving.contacts.front().relative_velocity.coords[0] - hit.contacts.front().relative_velocity.coords[0] - 4) < 1e-5f);

  // Ball crosses the forward leg region, well outside the upright body's x extent.
  const auto sliding = Body(e_FunctionType_Sliding);
  const auto leg_ball = State({.6f, -.4f, .18f}, {0, 80, 0});
  REQUIRE(Step(leg_ball, upright).contacts.empty());
  const auto leg = Step(leg_ball, sliding);
  REQUIRE(leg.contacts.front().collider == sliding[1].id);
  REQUIRE(leg.contacts.front().normal_impulse > 0);

  // Critical regression: a fallen player is NOT an upright blocker at chest height.
  const auto fallen = Body(e_FunctionType_Trip);
  const auto high_ball = State({-.4f, 0, 1.3f}, {80, 0, 0});
  REQUIRE(Step(high_ball, upright).contacts.front().normal_impulse > 0);
  REQUIRE(Step(high_ball, fallen).contacts.empty());
  const auto low = Step(State({.3f, -.4f, .24f}, {0, 80, 0}), fallen);
  REQUIRE(low.contacts.front().collider == fallen[0].id);
  REQUIRE(low.contacts.front().normal_impulse > 0);
  for (const auto& collider : fallen) {
    const auto& start = collider.start;
    REQUIRE(BodyShadowGap({0, 0, 1.3f}, .11f, start) > .5f);
  }
}

TEST_CASE("fixed shot pass trap boundaries expose passive foot conflict without accepting a touch", "[sim][body-poses]") {
  for (const auto type : {e_FunctionType_Shot, e_FunctionType_ShortPass, e_FunctionType_LongPass,
                         e_FunctionType_HighPass, e_FunctionType_Trap}) {
    PlayerActionDefinition definition;
    definition.type = type; definition.duration = TickSpan{30}; definition.contact = TickSpan{12};
    definition.contactPosition = {-.19f, 0, .11f};
    PlayerActionState action;
    PlayerActionExecutor::Begin(action, definition);
    REQUIRE(BodyActionTouchWindow(action) == BodyTouchWindow::Pending);
    PlayerActionExecutor::Step(action, TickSpan{11});
    REQUIRE(BodyActionTouchWindow(action) == BodyTouchWindow::Boundary);
    const auto body = Body(type);
    const auto result = Step(State({-.4f, 0, .11f}, {80, 0, 0}), body);
    REQUIRE(result.contacts.front().collider == body[1].id);
    REQUIRE(result.contacts.front().normal_impulse > 0);
    REQUIRE((result.contacts.front().point - action.contactPosition).GetLength() < .12f);
    BodyImpactClassification c;
    c.action = type; c.window = BodyActionTouchWindow(action);
    c.foot_conflict = BodyFootAction(type) && c.window == BodyTouchWindow::Boundary;
    c.legacy_blocks = BodyActionExcluded;
    BodyImpactGroup group; group.Record(c, false);
    REQUIRE(group.foot_conflicts == 1);
    REQUIRE(group.blocks[3] == 1);
    PlayerActionExecutor::Step(action, TickSpan{1});
    REQUIRE(BodyActionTouchWindow(action) == BodyTouchWindow::Boundary);
    PlayerActionExecutor::Step(action, TickSpan{1});
    REQUIRE(BodyActionTouchWindow(action) == BodyTouchWindow::Past);
    // No active impulse is generated or applied at this stage. P5a owns that.
  }
  REQUIRE_FALSE(BodyFootAction(e_FunctionType_Header));
  PlayerActionState max;
  max.contact = TickSpan{std::numeric_limits<std::uint64_t>::max()};
  max.elapsed = *max.contact;
  REQUIRE(BodyActionTouchWindow(max) == BodyTouchWindow::Boundary);
}

TEST_CASE("two contestants and several body parts compete by stable ids not input order", "[sim][body-poses]") {
  PlayerKinematicState a, b; a.position = {0, -.25f, 0}; b.position = {0, .25f, 0};
  const auto ma = Body(e_FunctionType_Movement, a, 0), mb = Body(e_FunctionType_Movement, b, 1);
  std::vector<ColliderMotion> motions(ma.begin(), ma.end());
  motions.insert(motions.end(), mb.begin(), mb.end());
  const auto state = State({-.4f, 0, .6f}, {80, 0, 0});
  REQUIRE(SweepBall(state, ma[1], .01f, .11f).has_value());
  REQUIRE(SweepBall(state, mb[1], .01f, .11f).has_value());
  const auto first = Step(state, motions);
  std::reverse(motions.begin(), motions.end());
  const auto reversed = Step(state, motions);
  REQUIRE(first.contacts.front().collider == ma[1].id);
  REQUIRE(first.contacts.front().collider == reversed.contacts.front().collider);
  Near(first.state.position, reversed.state.position);
  Near(first.state.velocity, reversed.state.velocity);
  // Upright upper and lower volumes overlap around .9m: both candidates, one response.
  const auto overlap = State({-.2f, 0, .9f}, {10, 0, 0});
  const auto body = Body(e_FunctionType_Movement);
  REQUIRE(SweepBall(overlap, body[0], .01f, .11f).has_value());
  REQUIRE(SweepBall(overlap, body[1], .01f, .11f).has_value());
  REQUIRE(Step(overlap, body).contacts.size() == 1);
}

TEST_CASE("fixed collider tapes continuously separate with identical permutation and rotation replay", "[sim][body-trajectory]") {
  for (const auto action : {e_FunctionType_Movement, e_FunctionType_Sliding, e_FunctionType_Trip}) {
    Ball forward{football::model::Pitch{}}, reordered{football::model::Pitch{}}, rotated{football::model::Pitch{}};
    const auto initial = State({-1.3f, 0, action == e_FunctionType_Movement ? .6f : .24f}, {8, 0, 0});
    forward.Reset(initial); reordered.Reset(initial); rotated.Reset(initial); rotated.Mirror();
    std::size_t body_impacts = 0;
    int first_impact = -1;
    for (int step = 0; step < 120; ++step) {
      PlayerKinematicState k; // fixed tape; no actors or RNG are executed
      auto body = Body(action, k);
      const auto result = forward.Step(BallTickInput{body, {}});
      std::reverse(body.begin(), body.end());
      const auto other = reordered.Step(BallTickInput{body, {}});
      for (auto& collider : body) Mirror(collider);
      const auto mirror = rotated.Step(BallTickInput{body, {}});
      Near(result.state.position, other.state.position); Near(result.state.velocity, other.state.velocity);
      Near(result.state.angular_velocity, other.state.angular_velocity);
      auto position = mirror.state.position, velocity = mirror.state.velocity, spin = mirror.state.angular_velocity;
      position.Mirror(); velocity.Mirror(); spin.Mirror();
      Near(position, result.state.position); Near(velocity, result.state.velocity); Near(spin, result.state.angular_velocity);
      REQUIRE(result.contacts.size() == other.contacts.size());
      REQUIRE(result.contacts.size() == mirror.contacts.size());
      for (std::size_t i = 0; i < result.contacts.size(); ++i) {
        const auto& hit = result.contacts[i];
        REQUIRE(hit.collider == other.contacts[i].collider);
        REQUIRE(hit.collider == mirror.contacts[i].collider);
        REQUIRE(std::fabs(hit.toi - mirror.contacts[i].toi) < 1e-5f);
        if (hit.collider >= kFirstDynamicBodyColliderId && hit.normal_impulse > 1e-6f) {
          ++body_impacts; if (first_impact < 0) first_impact = step;
        }
      }
      CAPTURE(action, step, first_impact, body_impacts, forward.state().position, forward.state().velocity);
      if (first_impact >= 0 && step > first_impact + 3) {
        for (const auto& collider : body) REQUIRE(BodyShadowGap(rotated.state().position, .11f, collider.end) > 0);
      }
      REQUIRE(std::isfinite(result.state.angular_velocity.GetLength()));
    }
    REQUIRE(body_impacts == 1);
    REQUIRE(first_impact >= 0);
    REQUIRE(forward.state().velocity.GetLength() < initial.velocity.GetLength());
  }
}

TEST_CASE("deep separating overlap projects once but does not invent a followup impact", "[sim][body-trajectory]") {
  const auto body = Body(e_FunctionType_Movement);
  const auto lower = std::span<const ColliderMotion>{body}.subspan(1, 1);
  Ball ball{football::model::Pitch{}};
  ball.Reset(State({.1f, 0, .6f}, {4, 0, 0}));
  const auto projection = ball.Step(BallTickInput{lower, {}});
  REQUIRE(projection.contacts.front().position_corrected);
  REQUIRE(projection.contacts.front().normal_impulse == 0);
  const float initial_spin = ball.state().angular_velocity.GetLength();
  for (int tick = 0; tick < 20; ++tick) {
    const auto result = ball.Step(BallTickInput{lower, {}});
    for (const auto& hit : result.contacts) REQUIRE(hit.collider != body[1].id);
    REQUIRE(BodyShadowGap(ball.state().position, .11f, lower.front().end) >= -1e-5f);
    REQUIRE(ball.state().angular_velocity.GetLength() == initial_spin);
  }
}

TEST_CASE("moving-body tape characterizes separating projection freeze as a switch blocker", "[sim][body-trajectory]") {
  Ball ball{football::model::Pitch{}};
  ball.Reset(State({-.4f, 0, .6f}, {20, 0, 0}));
  float outgoing = 0;
  for (int step = 0; step < 12; ++step) {
    PlayerKinematicState k; k.position = {-step * .01f, 0, 0}; k.velocity = {-1, 0, 0};
    const auto body = Body(e_FunctionType_Movement, k);
    const auto lower = std::span<const ColliderMotion>{body}.subspan(1, 1);
    const auto result = ball.Step(BallTickInput{lower, {}});
    REQUIRE(result.contacts.size() == 1);
    const auto& hit = result.contacts.front();
    if (step == 0) {
      REQUIRE(hit.normal_impulse > 0);
      outgoing = result.state.velocity.coords[0];
      REQUIRE(outgoing < -7);
    } else {
      // Current no-remainder semantics project to START geometry and consume
      // the tick even when relative normal velocity is separating. The moving
      // end shape then overlaps again. Do NOT hide this with a global cooldown.
      REQUIRE(hit.normal_impulse == 0);
      REQUIRE(hit.position_corrected);
      REQUIRE(hit.toi == 0);
      REQUIRE(std::fabs(result.state.velocity.coords[0] - outgoing) < 1e-5f);
      REQUIRE(std::fabs(result.state.position.coords[0] - (-step * .01f - .3f)) < 1e-5f);
      REQUIRE(BodyShadowGap(result.state.position, .11f, lower.front().end) < -.009f);
      // Proper free separating motion would clear the END geometry.
      REQUIRE(BodyShadowGap(result.state.position + result.state.velocity * .01f, .11f, lower.front().end) > .05f);
    }
  }
  // This is a passing characterization test of a KNOWN failure of physical
  // separation, not a claim of production readiness. Kernel behavior unchanged.
}

TEST_CASE("low pose axes convert explicitly when legacy kinematic facing does not mirror", "[sim][body-poses]") {
  PlayerKinematicState k; k.position = {2, 3, 0}; k.velocity = {4, 2, 0}; k.bodyFacing = {.6f, .8f, 0};
  auto mirrored = k; mirrored.Mirror();
  REQUIRE(mirrored.bodyFacing == k.bodyFacing);
  auto axis = k.bodyFacing; axis.Mirror();
  for (const auto action : {e_FunctionType_Sliding, e_FunctionType_Trip}) {
    auto original = Body(action, k, 5, k.bodyFacing);
    const auto converted = Body(action, mirrored, 5, axis);
    for (std::size_t part = 0; part < original.size(); ++part) {
      Mirror(original[part]);
      REQUIRE(original[part].id == converted[part].id);
      for (bool end : {false, true}) {
        const auto& a = end ? original[part].end : original[part].start;
        const auto& b = end ? converted[part].end : converted[part].start;
        if (part == 2) Near(std::get<Sphere>(a).center, std::get<Sphere>(b).center);
        else {
          Near(std::get<Capsule>(a).a, std::get<Capsule>(b).a);
          Near(std::get<Capsule>(a).b, std::get<Capsule>(b).b);
        }
      }
    }
  }
}
