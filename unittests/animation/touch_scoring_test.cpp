#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "football/animation/touch_scoring.hpp"

using Catch::Approx;
using football::animation::ResolveActionSmuggle;
using football::animation::ResolveActionSmuggleRequest;

namespace {

ResolveActionSmuggleRequest BaseRequest() {
  ResolveActionSmuggleRequest request;
  request.raw_smuggle = Vector3(1, 0, 0);
  request.function_type = e_FunctionType_BallControl;
  request.enable_action_smuggle_discard = true;
  request.cheat_discard_distance = 0.02f;
  request.cheat_discard_distance_multiplier = 0.4f;
  request.max_smuggle_discard_distance = 0.2f;
  request.discard_forward_smuggle = true;
  request.discard_sideways_smuggle = true;
  request.closest_opponent_distance = 1.2f;
  return request;
}

}  // namespace

TEST_CASE("smuggle is untouched when no discard mode is active") {
  ResolveActionSmuggleRequest request = BaseRequest();
  request.enable_action_smuggle_discard = false;
  const Vector3 out = ResolveActionSmuggle(request);
  CHECK(out.coords[0] == Approx(1.0f));
  CHECK(out.coords[1] == Approx(0.0f));
}

TEST_CASE("full discard zeroes the smuggle") {
  ResolveActionSmuggleRequest request = BaseRequest();
  request.force_full_action_smuggle_discard = true;
  const Vector3 out = ResolveActionSmuggle(request);
  CHECK(out.GetLength() == Approx(0.0f));
}

TEST_CASE("a retained ball zeroes the smuggle") {
  ResolveActionSmuggleRequest request = BaseRequest();
  request.ball_retained_by_player = true;
  const Vector3 out = ResolveActionSmuggle(request);
  CHECK(out.GetLength() == Approx(0.0f));
}

TEST_CASE("the discard chain shortens, drops sideways and scales with pressure") {
  // distance: clamp(1 - 0.02) = 0.98, * (1 - 0.4) = 0.588, min(_, 0.8) = 0.588
  // sideways discard: 0.588 * 0.7 = 0.4116, pressure factor at 1.2 is 1.0
  const Vector3 out = ResolveActionSmuggle(BaseRequest());
  CHECK(out.coords[0] == Approx(0.4116f).margin(1e-5f));
  CHECK(out.coords[1] == Approx(0.0f));
  CHECK(out.coords[2] == Approx(0.0f));
}

TEST_CASE("the non-BallControl multiplier is steeper") {
  ResolveActionSmuggleRequest request = BaseRequest();
  request.function_type = e_FunctionType_Shot;
  // 0.98 * (1 - 0.4 * 0.8) = 0.6664, then forward discard adds allowForwardDistance
  // 0.1f for non-dribble: coords[1] is 0 so it stays 0; sideways 0.6664*0.7 = 0.46648
  const Vector3 out = ResolveActionSmuggle(request);
  CHECK(out.coords[0] == Approx(0.46648f).margin(1e-5f));
}

TEST_CASE("ResolveActionSmuggle is deterministic") {
  const ResolveActionSmuggleRequest request = BaseRequest();
  const Vector3 a = ResolveActionSmuggle(request);
  const Vector3 b = ResolveActionSmuggle(request);
  CHECK(a.coords[0] == b.coords[0]);
  CHECK(a.coords[1] == b.coords[1]);
  CHECK(a.coords[2] == b.coords[2]);
}
