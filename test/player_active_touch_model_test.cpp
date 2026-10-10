#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "football/ball/ball.hpp"
#include "sim/player/player_active_touch_model.hpp"

using football::sim::player::ProposeActiveTouch;
using football::sim::player::TouchProposalInput;
using football::sim::player::TouchRejection;
using football::sim::player::TouchTechnique;
using blunted::Vector3;

namespace {
TouchProposalInput Base() {
  TouchProposalInput input;
  input.passive_endpoint = football::ball::BallState{
      {0, 0, .11f}, {0, 0, 0}, Vector3(0), blunted::Quaternion{}};
  input.desired_ball_center = {0, 0, .11f};
  input.target_velocity = {30, 0, 0};
  return input;
}
float SideSpin(const Vector3& spin) { return spin.coords[2]; }
float TopSpin(const Vector3& spin) { return spin.coords[1]; }
}

TEST_CASE("active touch reachability rejects before any surface projection",
          "[active-touch][model]") {
  auto input = Base();
  const auto reachable = ProposeActiveTouch(input);
  REQUIRE(reachable.reachable);
  REQUIRE(reachable.rejection == TouchRejection::None);
  REQUIRE(reachable.velocity_error < 1e-5f);
  REQUIRE(reachable.surface_error < 1e-6f);

  input.desired_ball_center = {2, 0, .11f};
  const auto far = ProposeActiveTouch(input);
  REQUIRE_FALSE(far.reachable);
  REQUIRE(far.rejection == TouchRejection::Distance);
  REQUIRE(far.impulse.impulse == Vector3(0));
  REQUIRE(far.contact_point == Vector3(0));

  input = Base();
  input.reach = 5.0f;
  input.desired_ball_center = {.2f, 0, 3.0f};
  const auto high = ProposeActiveTouch(input);
  REQUIRE_FALSE(high.reachable);
  REQUIRE(high.rejection == TouchRejection::Height);

  input = Base();
  input.target_velocity = Vector3(0);
  REQUIRE(ProposeActiveTouch(input).rejection == TouchRejection::Direction);

  // An illegal technique offset beyond the ball silhouette is rejected.
  input = Base();
  input.technique = TouchTechnique{.2f, 0};
  REQUIRE(ProposeActiveTouch(input).rejection == TouchRejection::SurfaceRange);
  // A legal offset always lands exactly on the surface.
  input.technique = TouchTechnique{.08f, .04f};
  REQUIRE(ProposeActiveTouch(input).surface_error < 1e-6f);
}

TEST_CASE("centre strike is torque-free and technique offsets set spin direction",
          "[active-touch][model]") {
  auto input = Base();
  const auto laces = ProposeActiveTouch(input);
  REQUIRE(laces.spin.GetLength() < 1e-6f);
  // A moving incoming ball still cannot CREATE spin through a centre strike:
  // only the incoming spin is carried through the response.
  auto incoming = Base();
  incoming.passive_endpoint.velocity = {-12, 5, 0};
  incoming.passive_endpoint.angular_velocity = {0, 0, 40};
  const auto incoming_result = ProposeActiveTouch(incoming);
  REQUIRE((incoming_result.spin - incoming.passive_endpoint.angular_velocity).GetLength() <
          1e-5f);

  // Inside foot: contact right of the strike line curls one way.
  auto inside = Base();
  inside.technique = TouchTechnique{.07f, 0};
  const auto inside_result = ProposeActiveTouch(inside);
  REQUIRE(std::fabs(SideSpin(inside_result.spin)) > 1.0f);
  // Outside foot mirrors the sidespin sign.
  auto outside = Base();
  outside.technique = TouchTechnique{-.07f, 0};
  const auto outside_result = ProposeActiveTouch(outside);
  REQUIRE(std::fabs(SideSpin(outside_result.spin) + SideSpin(inside_result.spin)) <
          std::fabs(SideSpin(inside_result.spin)) * 1e-3f);
  // Vertical offsets give opposing top/back spin.
  auto top = Base();
  top.technique = TouchTechnique{0, .07f};
  auto back = Base();
  back.technique = TouchTechnique{0, -.07f};
  const auto top_spin = TopSpin(ProposeActiveTouch(top).spin);
  const auto back_spin = TopSpin(ProposeActiveTouch(back).spin);
  REQUIRE(std::fabs(top_spin) > 1.0f);
  REQUIRE(std::fabs(top_spin + back_spin) < std::fabs(top_spin) * 1e-3f);
  // Only the offset sign matters for direction, not the request magnitude: the
  // linear response is always the requested target velocity.
  REQUIRE(inside_result.velocity_error < 1e-5f);
  REQUIRE((inside_result.response.velocity - Vector3(30, 0, 0)).GetLength() < 1e-5f);
}

TEST_CASE("left/right mirror and moving versus static strikes", "[active-touch][model]") {
  auto right = Base();
  right.technique = TouchTechnique{.07f, .02f};
  const auto right_result = ProposeActiveTouch(right);
  // Mirror the whole problem in x: the mirrored spin must be the mirrored spin.
  auto left = Base();
  left.passive_endpoint.position.Mirror();
  left.passive_endpoint.velocity.Mirror();
  left.desired_ball_center.Mirror();
  left.target_velocity.Mirror();
  left.technique = TouchTechnique{.07f, .02f};
  const auto left_result = ProposeActiveTouch(left);
  Vector3 expected = right_result.spin;
  expected.Mirror();
  REQUIRE((left_result.spin - expected).GetLength() < 1e-4f);

  // Same strike against a moving player/ball: direction stays, magnitude scales
  // with the required impulse.
  auto moving = right;
  moving.passive_endpoint.velocity = {-6, 0, 0};
  const auto moving_result = ProposeActiveTouch(moving);
  REQUIRE(moving_result.velocity_error < 1e-5f);
  REQUIRE(SideSpin(moving_result.spin) * SideSpin(right_result.spin) > 0);
  REQUIRE(std::fabs(SideSpin(moving_result.spin)) > std::fabs(SideSpin(right_result.spin)));
}

TEST_CASE("magnus trajectory curves consistently and stays numerically stable",
          "[active-touch][model][magnus]") {
  const football::model::Pitch pitch;
  auto spin_shot = [&](float lateral, football::sim::TickSpan horizon) {
    TouchProposalInput input;
    input.passive_endpoint = football::ball::BallState{
        {0, 0, .3f}, {18, 0, 2}, Vector3(0), blunted::Quaternion{}};
    input.desired_ball_center = {0, 0, .3f};
    input.target_velocity = {25, 0, 4};
    input.technique = TouchTechnique{lateral, 0};
    const auto proposal = ProposeActiveTouch(input);
    REQUIRE(proposal.reachable);
    football::ball::Ball ball{pitch};
    ball.Reset(proposal.response);
    return std::make_pair(ball.Predict(horizon, football::ball::BallEnvironment{}),
                          proposal.spin);
  };
  const auto straight = spin_shot(0.0f, football::sim::TickSpan{100}).first;
  const auto curved = spin_shot(.08f, football::sim::TickSpan{100});
  REQUIRE(curved.second.coords[2] > 1.0f);  // sidespin exists
  // A sidespin strike deviates laterally, monotonically growing with time.
  const auto half = spin_shot(.08f, football::sim::TickSpan{50}).first;
  const auto one = spin_shot(.08f, football::sim::TickSpan{100}).first;
  const auto two = spin_shot(.08f, football::sim::TickSpan{200}).first;
  REQUIRE(std::fabs(half.position.coords[1]) > 1e-4f);
  REQUIRE(std::fabs(one.position.coords[1]) > std::fabs(half.position.coords[1]));
  REQUIRE(std::fabs(two.position.coords[1]) > std::fabs(one.position.coords[1]));
  REQUIRE(std::fabs(straight.position.coords[1]) < 1e-4f);
  // High spin must remain finite over two seconds.
  for (const auto& state : {half, one, two}) {
    REQUIRE(std::isfinite(state.position.GetLength()));
    REQUIRE(std::isfinite(state.velocity.GetLength()));
    REQUIRE(state.position.GetLength() < 400.0f);
  }
}
