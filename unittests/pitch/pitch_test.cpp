
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <string_view>
#include <variant>
#include <type_traits>

#include "football/foundation/geometry/collider.hpp"
#include "football/foundation/geometry/shapes.hpp"
#include "football/foundation/math/vector3.hpp"
#include "football/pitch/pitch.hpp"

using football::geometry::Capsule;
using football::geometry::Collider;
using football::geometry::HalfSpace;
using football::math::Vector3;
using football::pitch::Pitch;
using football::pitch::PitchModel;
using Catch::Approx;

static_assert(std::is_aggregate_v<PitchModel>);
static_assert(std::is_standard_layout_v<PitchModel>);
static_assert(std::is_trivially_copyable_v<PitchModel>);
static_assert(std::is_same_v<PitchModel, football_pitch_model>);

TEST_CASE("Default pitch builds seven ordered static colliders", "[unit][pitch]") {
  const Pitch pitch;
  REQUIRE(pitch.colliders().size() == 7);

  CHECK(pitch.colliders()[0].name == "ground");
  const std::array<std::string_view, 6> goal_parts{
      "goal_neg_x_neg_y_post", "goal_neg_x_pos_y_post", "goal_pos_x_neg_y_post",
      "goal_pos_x_pos_y_post", "goal_neg_x_crossbar", "goal_pos_x_crossbar"};
  for (std::size_t i = 0; i < goal_parts.size(); ++i)
    CHECK(pitch.colliders()[i + 1].name == goal_parts[i]);

  const Collider& ground = pitch.colliders().front();
  REQUIRE(std::holds_alternative<HalfSpace>(ground.shape));
  const auto& half_space = std::get<HalfSpace>(ground.shape);
  CHECK(half_space.point == Vector3(0.0f, 0.0f, 0.0f));
  CHECK(half_space.normal == Vector3(0.0f, 0.0f, 1.0f));

  for (std::size_t i = 1; i < pitch.colliders().size(); ++i) {
    CHECK(std::holds_alternative<Capsule>(pitch.colliders()[i].shape));
  }
}

TEST_CASE("Goal colliders follow the pitch geometry", "[unit][pitch]") {
  const Pitch pitch;
  const float x = pitch.half_length();
  const float y = pitch.goal_half_width();
  const float z = pitch.goal_height();

  const auto capsule = [&](std::size_t i) {
    return std::get<Capsule>(pitch.colliders()[i].shape);
  };

  CHECK(capsule(1).a == Vector3(-x, -y, 0.0f));
  CHECK(capsule(1).b == Vector3(-x, -y, z));
  CHECK(capsule(2).a == Vector3(-x, y, 0.0f));
  CHECK(capsule(3).a == Vector3(x, -y, 0.0f));
  CHECK(capsule(4).a == Vector3(x, y, 0.0f));
  for (std::size_t i = 1; i <= 4; ++i) {
    CHECK(capsule(i).radius == Approx(0.06f));
  }

  CHECK(capsule(5).a == Vector3(-x, -y, z));
  CHECK(capsule(5).b == Vector3(-x, y, z));
  CHECK(capsule(6).a == Vector3(x, -y, z));
  CHECK(capsule(6).b == Vector3(x, y, z));
}

TEST_CASE("Configuration drives the derived geometry", "[unit][pitch]") {
  PitchModel model{90.0f, 60.0f, 0.04f, 1.6f, 0.015f};
  const Pitch small(model);
  CHECK(small.model().length == model.length);
  CHECK(small.model().width == model.width);
  CHECK(small.model().quadratic_resistance == model.quadratic_resistance);
  CHECK(small.model().ground_deceleration == model.ground_deceleration);
  CHECK(small.model().air_drag == model.air_drag);
  CHECK(small.length() == Approx(90.0f));
  CHECK(small.width() == Approx(60.0f));
  CHECK(small.half_length() == Approx(45.0f));
  CHECK(small.half_width() == Approx(30.0f));
  CHECK(small.contains(45.0f, 30.0f));
  CHECK(small.contains(-45.0f, -30.0f));
  CHECK_FALSE(small.contains(45.1f, 0.0f));
  CHECK_FALSE(small.contains(0.0f, 30.1f));

  const auto post = std::get<Capsule>(small.colliders()[1].shape);
  CHECK(post.a.coords[0] == Approx(-45.0f));
}

TEST_CASE("Pitch equality compares configuration, not the collider cache",
          "[unit][pitch]") {
  const Pitch a;
  const Pitch b;
  CHECK(a == b);

  PitchModel model{100.0f, 68.0f, 0.04f, 1.6f, 0.015f};
  const Pitch other(model);
  CHECK(a != other);
  CHECK(other == Pitch(model));

  model = PitchModel{105.0f, 68.0f, 0.04f, 1.6f, 0.02f};
  const Pitch more_air(model);
  CHECK(a != more_air);
  CHECK(a.air_drag() == Approx(0.015f));
  CHECK(more_air.air_drag() == Approx(0.02f));
}

TEST_CASE("PitchModel defaults preserve the pitch configuration", "[unit][pitch]") {
  const PitchModel model{105.0f, 68.0f, 0.04f, 1.6f, 0.015f};
  CHECK(model.length == Approx(105.0f));
  CHECK(model.width == Approx(68.0f));
  CHECK(model.quadratic_resistance == Approx(0.04f));
  CHECK(model.ground_deceleration == Approx(1.6f));
  CHECK(model.air_drag == Approx(0.015f));
  const PitchModel copied = Pitch(model).model();
  CHECK(copied.length == model.length);
  CHECK(copied.width == model.width);
  CHECK(copied.quadratic_resistance == model.quadratic_resistance);
  CHECK(copied.ground_deceleration == model.ground_deceleration);
  CHECK(copied.air_drag == model.air_drag);
  const PitchModel def = Pitch{}.model();
  CHECK(def.length == model.length);
  CHECK(def.width == model.width);
  CHECK(def.quadratic_resistance == model.quadratic_resistance);
  CHECK(def.ground_deceleration == model.ground_deceleration);
  CHECK(def.air_drag == model.air_drag);
}

TEST_CASE("Pitch owns a copy of editable model values", "[unit][pitch]") {
  PitchModel model{90.0f, 60.0f, 0.04f, 1.6f, 0.0f};
  const Pitch pitch(model);
  const PitchModel held = pitch.model();
  CHECK(held.length == model.length);
  CHECK(held.width == model.width);
  CHECK(held.quadratic_resistance == model.quadratic_resistance);
  CHECK(held.ground_deceleration == model.ground_deceleration);
  CHECK(held.air_drag == model.air_drag);
  model.length = 0.0f;
  model.air_drag = -1.0f;
  CHECK(pitch.length() == Approx(90.0f));
  CHECK(pitch.air_drag() == Approx(0.0f));
}

TEST_CASE("Shared pitch model has no hidden defaults", "[unit][pitch]") {
  const PitchModel zero{};
  CHECK(zero.length == 0.0f);
  CHECK(zero.width == 0.0f);
  CHECK(zero.quadratic_resistance == 0.0f);
  CHECK(zero.ground_deceleration == 0.0f);
  CHECK(zero.air_drag == 0.0f);
}
