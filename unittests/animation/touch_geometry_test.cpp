#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "football/animation/touch_geometry.hpp"

using Catch::Approx;
using football::animation::EvaluateTouchGeometry;
using football::animation::FrontOfFootOffsetRel;
using football::animation::SegmentProjectionParameter;
using football::animation::TouchGeometryRequest;

namespace {

TouchGeometryRequest AlignedRequest() {
  TouchGeometryRequest request;
  request.anim_ball_pos = Vector3(0, 0, 0.11f);
  request.anim_body_pos = Vector3(0, 0, 0);
  request.touch_body_pos_rotated = Vector3(0, 0, 0);
  request.ball_pos = Vector3(0, 0, 0.11f);
  request.ball_movement = Vector3(0, 0, 0);
  request.touch_movement = Vector3(0, 0, 0);
  request.anim_touch_frame = 12;
  request.frame_count = 24;
  request.default_touch_frame = 12;
  request.height_scale = 1.0f;
  request.touch_offset_ms = 80.0f;
  return request;
}

}  // namespace

TEST_CASE("segment projection keeps the legacy 2D/3D formula") {
  CHECK(SegmentProjectionParameter(Vector3(0, 0, 0), Vector3(10, 0, 0), Vector3(5, 5, 0)) ==
        Approx(0.5f));
  CHECK(SegmentProjectionParameter(Vector3(1, 1, 0), Vector3(1, 1, 0), Vector3(4, 4, 0)) ==
        Approx(0.0f));
}

TEST_CASE("an aligned contact is within reach") {
  const auto result = EvaluateTouchGeometry(AlignedRequest());
  CHECK(result.within_reach);
  CHECK(result.ball_distance_z == Approx(0.0f));
  CHECK(result.action_smuggle_vec2d.GetLength() == Approx(0.0f));
  CHECK(result.anim_ball_pos.coords[2] == Approx(0.11f));
}

TEST_CASE("the touch-frame factor and radius factor follow the legacy chain") {
  const auto result = EvaluateTouchGeometry(AlignedRequest());
  // 0.3 * pow(12/24, 0.7) == 0.1846716...
  CHECK(result.touch_framed_radius_factor == Approx(0.1846716f).margin(1e-6f));
  CHECK(result.radius_cheat_offset == Approx(0.0f));
}

TEST_CASE("a deflect widens the radius by 1.8") {
  TouchGeometryRequest request = AlignedRequest();
  request.function_type = e_FunctionType_Deflect;
  const auto result = EvaluateTouchGeometry(request);
  CHECK(result.touch_framed_radius_factor == Approx(0.1846716f * 1.8f).margin(1e-6f));
  CHECK(result.radius_cheat_offset == Approx(0.4f));
}

TEST_CASE("a high ball is out of reach") {
  TouchGeometryRequest request = AlignedRequest();
  request.ball_pos = Vector3(0, 0, 3.0f);
  const auto result = EvaluateTouchGeometry(request);
  CHECK_FALSE(result.within_reach);
  CHECK(result.ball_distance_z > 0.22f);
}

TEST_CASE("the continuous ball check moves ball_pos onto the segment") {
  TouchGeometryRequest request = AlignedRequest();
  request.use_continuous_ball_check = true;
  request.ball_pos = Vector3(0, 0, 0.11f);
  request.ball_movement = Vector3(0, 100, 0);
  const auto result = EvaluateTouchGeometry(request);
  CHECK(result.ball_pos.coords[1] == Approx(0.0f).margin(1e-6f));
}

TEST_CASE("the front-of-foot offset matches the legacy expression") {
  const Vector3 ffo = FrontOfFootOffsetRel(0.0f, 0.0f, 0.11f, 80.0f);
  CHECK(ffo.coords[0] == Approx(0.0f));
  CHECK(ffo.coords[1] == Approx(-0.34f));
}

TEST_CASE("EvaluateTouchGeometry is deterministic") {
  const TouchGeometryRequest request = AlignedRequest();
  const auto a = EvaluateTouchGeometry(request);
  const auto b = EvaluateTouchGeometry(request);
  CHECK(a.touch_framed_radius_factor == b.touch_framed_radius_factor);
  CHECK(a.ball_distance_z == b.ball_distance_z);
}
