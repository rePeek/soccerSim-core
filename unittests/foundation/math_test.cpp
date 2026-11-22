#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <type_traits>

#include "football/foundation/math/quaternion.hpp"
#include "football/foundation/math/scalar.hpp"
#include "football/foundation/math/vector3.hpp"

using Catch::Approx;
using football::math::Quaternion;
using football::math::Vector3;
using football::math::pi;
using football::math::real;

static_assert(!std::is_assignable_v<Vector3&, const Quaternion&>);

namespace {

Quaternion AxisRotation(float angle, const Vector3& unit_axis) {
  Quaternion rotation;
  rotation.SetAngleAxis(angle, unit_axis);
  return rotation;
}

void RequireSameQuaternionComponents(const Quaternion& actual, const Quaternion& expected) {
  for (int component = 0; component < 4; ++component) {
    REQUIRE(actual.elements[component] == Approx(expected.elements[component]).margin(1e-6f));
  }
}

}

TEST_CASE("Euler YZX construction matches qY times qZ times qX") {
  for (const auto& angles : std::array{
           Vector3(0.2f, 0.3f, 0.4f), Vector3(-0.7f, 0.5f, -0.6f),
           Vector3(0.0f, 0.0f, 0.0f)}) {
    const auto x_rotation = AxisRotation(angles.coords[0], {1, 0, 0});
    const auto y_rotation = AxisRotation(angles.coords[1], {0, 1, 0});
    const auto z_rotation = AxisRotation(angles.coords[2], {0, 0, 1});
    Quaternion actual;
    actual.SetEulerAnglesYZX(angles.coords[0], angles.coords[1], angles.coords[2]);
    RequireSameQuaternionComponents(actual, y_rotation * z_rotation * x_rotation);
  }
}

TEST_CASE("Euler XYZ extraction recovers nonsingular qX times qY times qZ angles") {
  for (const auto& angles : std::array{
           Vector3(0.2f, 0.3f, 0.4f), Vector3(-0.7f, 0.5f, -0.6f),
           Vector3(0.0f, 0.0f, 0.0f)}) {
    const auto rotation = AxisRotation(angles.coords[0], {1, 0, 0}) *
                          AxisRotation(angles.coords[1], {0, 1, 0}) *
                          AxisRotation(angles.coords[2], {0, 0, 1});
    real x_radians, y_radians, z_radians;
    rotation.GetEulerAnglesXYZ(x_radians, y_radians, z_radians);
    CHECK(x_radians == Approx(angles.coords[0]).margin(1e-6f));
    CHECK(y_radians == Approx(angles.coords[1]).margin(1e-6f));
    CHECK(z_radians == Approx(angles.coords[2]).margin(1e-6f));
  }
}

TEST_CASE("a quaternion produces an explicitly rotated negative Z direction") {
  CHECK(Vector3::FromRotatedNegativeZAxis(Quaternion()) == Vector3(0, 0, -1));
  const auto rotation = AxisRotation(pi * 0.5f, {0, 1, 0});
  const auto direction = Vector3::FromRotatedNegativeZAxis(rotation);
  CHECK(direction.coords[0] == Approx(-1.0f).margin(1e-6f));
  CHECK(direction.coords[1] == Approx(0.0f).margin(1e-6f));
  CHECK(direction.coords[2] == Approx(0.0f).margin(1e-6f));
}

TEST_CASE("azimuth wraps to a positive turn and signed XY angle starts at the reference") {
  const Vector3 positive_x(1, 0, 0);
  const Vector3 positive_y(0, 1, 0);
  const Vector3 negative_y(0, -1, 0);
  CHECK(real(positive_x.GetAzimuthRadians()) == Approx(0.0f));
  CHECK(real(positive_y.GetAzimuthRadians()) == Approx(pi * 0.5f));
  CHECK(real(negative_y.GetAzimuthRadians()) == Approx(pi * 1.5f));
  CHECK(real(positive_y.GetSignedAngleFromXY(positive_x)) == Approx(pi * 0.5f));
  CHECK(real(positive_x.GetSignedAngleFromXY(positive_y)) == Approx(-pi * 0.5f));
}

TEST_CASE("length clamping never stretches shorter vectors") {
  Vector3 longer(3, 4, 0);
  const Vector3 shorter(0.5f, 0, 0);
  CHECK(shorter.GetLengthClamped(2.0f) == shorter);
  CHECK(longer.GetLengthClamped(2.0f).GetLength() == Approx(2.0f));
  CHECK(longer == Vector3(3, 4, 0));
  longer.ClampLength(2.0f);
  CHECK(longer.GetLength() == Approx(2.0f));
}

TEST_CASE("normalization preserves zero or uses an explicit fallback") {
  Vector3 zero;
  zero.Normalize();
  CHECK(zero == Vector3(0));
  zero.FastNormalize();
  CHECK(zero == Vector3(0));
  zero.Normalize({0, 0, 1});
  CHECK(zero == Vector3(0, 0, 1));

  Vector3 approximate(3, 4, 0);
  approximate.FastNormalize();
  CHECK(approximate.GetLength() == Approx(1.0f).margin(0.002f));
}

TEST_CASE("extrapolation converts milliseconds to seconds") {
  Vector3 position(1, 2, 3);
  position.Extrapolate({4, 0, -2}, 250);
  CHECK(position == Vector3(2, 2, 2.5f));
}

TEST_CASE("quaternion conjugation and hemisphere matching keep their explicit effects") {
  auto rotation = AxisRotation(0.4f, {0, 1, 0});
  const auto conjugated = rotation.GetConjugated();
  rotation.Conjugate();
  CHECK(rotation == conjugated);

  Quaternion opposite = -conjugated;
  CHECK(opposite.MatchHemisphere(conjugated) == Approx(1.0f));
  CHECK(opposite == conjugated);
}

TEST_CASE("normalized clamp maps a bounded interval to zero through one") {
  CHECK(football::math::NormalizedClamp(-1, 0, 10) == 0.0f);
  CHECK(football::math::NormalizedClamp(5, 0, 10) == 0.5f);
  CHECK(football::math::NormalizedClamp(11, 0, 10) == 1.0f);
}
