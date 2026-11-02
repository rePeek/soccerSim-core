#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "football/player/execution/player_touch_geometry.hpp"
#include "football/player/execution/player_movement_math.hpp"

using Catch::Approx;
using football::player::EvaluateTouchGeometry;
using football::player::FrontOfFootOffsetRel;
using football::player::TouchGeometryRequest;
using football::math::Vector3;

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

TEST_CASE("retention and recent-touch skill affect Player touch tolerance") {
  auto request = AlignedRequest();
  request.ball_pos.coords[2] = 3.0f;
  CHECK_FALSE(EvaluateTouchGeometry(request).within_reach);
  request.ball_retained_by_player = true;
  CHECK(EvaluateTouchGeometry(request).within_reach);

  request = AlignedRequest();
  request.function_type = e_FunctionType_Shot;
  const auto normal = EvaluateTouchGeometry(request);
  request.last_touch_bias = 1.0f;
  const auto recent = EvaluateTouchGeometry(request);
  CHECK(recent.radius_cheat_offset == Approx(normal.radius_cheat_offset * 0.03f));
  CHECK(recent.touch_framed_radius_factor ==
        Approx(normal.touch_framed_radius_factor * 0.03f));
  request.technical_ballcontrol = 1.0f;
  const auto skilled = EvaluateTouchGeometry(request);
  CHECK(skilled.radius_cheat_offset > recent.radius_cheat_offset);
  CHECK(skilled.touch_framed_radius_factor < normal.touch_framed_radius_factor);
}

TEST_CASE("pass preference narrows control reach without changing pass reach") {
  auto request = AlignedRequest();
  for (const auto type : {e_FunctionType_Trap, e_FunctionType_BallControl,
                          e_FunctionType_ShortPass, e_FunctionType_LongPass,
                          e_FunctionType_HighPass, e_FunctionType_Shot,
                          e_FunctionType_Sliding, e_FunctionType_Interfere}) {
    request.function_type = type;
    request.prefer_pass_and_shot = false;
    const auto normal = EvaluateTouchGeometry(request);
    request.prefer_pass_and_shot = true;
    const auto preferred = EvaluateTouchGeometry(request);
    const float factor = (type == e_FunctionType_Trap ||
                          type == e_FunctionType_BallControl) ? 0.3f : 1.0f;
    CHECK(preferred.touch_framed_radius_factor ==
          Approx(normal.touch_framed_radius_factor * factor));
    CHECK(preferred.radius_cheat_offset == normal.radius_cheat_offset);
  }
}

TEST_CASE("fast cornering is a speed-weighted Player selection preference") {
  using football::player::CorneringBiasForFastCornering;
  const Vector3 fast(0, -7.5f, 0);
  CHECK(CorneringBiasForFastCornering(fast, fast) == Approx(0.0f));
  CHECK(CorneringBiasForFastCornering(fast, -fast) == Approx(1.0f));
  CHECK(CorneringBiasForFastCornering(fast, Vector3(7.5f, 0, 0)) == Approx(0.5f));
  CHECK(CorneringBiasForFastCornering(fast * 0.5f, -fast) == Approx(0.5f));
  CHECK(CorneringBiasForFastCornering(fast, -fast, 1.0f, 0.9f) == Approx(0.9f));
}
