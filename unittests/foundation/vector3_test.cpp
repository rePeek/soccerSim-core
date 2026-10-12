// Vector3: only mathematical properties and the documented boundary cases.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "football/foundation/math/quaternion.hpp"
#include "football/foundation/math/vector3.hpp"

using blunted::Quaternion;
using blunted::Vector3;
using Catch::Approx;

TEST_CASE("Vector3 length and squared length", "[unit][foundation][vector3]") {
  const Vector3 v{3.0f, 4.0f, 0.0f};
  CHECK(v.GetLength() == Approx(5.0f));
  CHECK(v.GetSquaredLength() == Approx(25.0f));
}

TEST_CASE("Vector3 normalization has unit length and keeps direction",
          "[unit][foundation][vector3]") {
  const Vector3 v{3.0f, 4.0f, 0.0f};
  const Vector3 n = v.GetNormalized();
  CHECK(n.GetLength() == Approx(1.0f));
  CHECK(n.GetDotProduct(v) == Approx(5.0f));
}

TEST_CASE("Vector3 normalization falls back for the zero vector",
          "[unit][foundation][vector3]") {
  const Vector3 zero{0.0f, 0.0f, 0.0f};
  const Vector3 fallback{0.0f, -1.0f, 0.0f};
  CHECK(zero.GetNormalized(fallback) == fallback);
}

TEST_CASE("Vector3 cross product is perpendicular and right-handed",
          "[unit][foundation][vector3]") {
  const Vector3 x{1.0f, 0.0f, 0.0f};
  const Vector3 y{0.0f, 1.0f, 0.0f};
  const Vector3 z = x.GetCrossProduct(y);
  CHECK(z.coords[0] == Approx(0.0f));
  CHECK(z.coords[1] == Approx(0.0f));
  CHECK(z.coords[2] == Approx(1.0f));
  CHECK(z.GetDotProduct(x) == Approx(0.0f));
  CHECK(z.GetDotProduct(y) == Approx(0.0f));
}

TEST_CASE("Vector3 dot product and distance", "[unit][foundation][vector3]") {
  const Vector3 a{1.0f, 2.0f, 3.0f};
  const Vector3 b{-1.0f, 4.0f, 0.0f};
  CHECK(a.GetDotProduct(b) == Approx(7.0f).margin(1e-6f));
  CHECK(a.GetDistance(b) == Approx(std::sqrt(4.0f + 4.0f + 9.0f)).margin(1e-6f));
}

TEST_CASE("Vector3 Mirror flips x and y but not z", "[unit][foundation][vector3]") {
  Vector3 v{1.0f, 2.0f, 3.0f};
  v.Mirror();
  CHECK(v.coords[0] == Approx(-1.0f));
  CHECK(v.coords[1] == Approx(-2.0f));
  CHECK(v.coords[2] == Approx(3.0f));
}

TEST_CASE("Vector3 2D rotation is a proper rotation", "[unit][foundation][vector3]") {
  const Vector3 x{1.0f, 0.0f, 0.0f};
  const Vector3 y = x.GetRotated2D(blunted::pi * 0.5f);
  CHECK(y.coords[0] == Approx(0.0f).margin(1e-6f));
  CHECK(y.coords[1] == Approx(1.0f).margin(1e-6f));
  CHECK(y.coords[2] == Approx(0.0f));
  CHECK(y.GetLength() == Approx(1.0f));
}

TEST_CASE("Vector3 2D angle uses the x/y plane", "[unit][foundation][vector3]") {
  CHECK(float(Vector3(1.0f, 0.0f, 0.0f).GetAngle2D()) == Approx(0.0f).margin(1e-6f));
  CHECK(float(Vector3(0.0f, 1.0f, 0.0f).GetAngle2D()) == Approx(blunted::pi * 0.5f).margin(1e-6f));
}

TEST_CASE("Vector3 identity quaternion rotation is a no-op",
          "[unit][foundation][vector3]") {
  Vector3 v{0.3f, -2.0f, 1.4f};
  const Vector3 before = v;
  v.Rotate(Quaternion());
  CHECK(v.coords[0] == Approx(before.coords[0]).margin(1e-5f));
  CHECK(v.coords[1] == Approx(before.coords[1]).margin(1e-5f));
  CHECK(v.coords[2] == Approx(before.coords[2]).margin(1e-5f));
}

TEST_CASE("Vector3 quarter turn about z maps +x to +y", "[unit][foundation][vector3]") {
  const float s = std::sin(blunted::pi * 0.25f);
  const float c = std::cos(blunted::pi * 0.25f);
  const Quaternion quarter_turn(0.0f, 0.0f, s, c);

  Vector3 v{1.0f, 0.0f, 0.0f};
  v.Rotate(quarter_turn);
  CHECK(v.coords[0] == Approx(0.0f).margin(1e-5f));
  CHECK(v.coords[1] == Approx(1.0f).margin(1e-5f));
  CHECK(v.coords[2] == Approx(0.0f).margin(1e-5f));
}

TEST_CASE("Vector3 quaternion rotation preserves length", "[unit][foundation][vector3]") {
  const Quaternion q = Quaternion(0.2f, 0.5f, -0.4f, 0.7f).GetNormalized();
  Vector3 v{1.0f, 2.0f, -0.5f};
  const float before = v.GetLength();
  v.Rotate(q);
  CHECK(v.GetLength() == Approx(before).margin(1e-5f));
}

TEST_CASE("Vector3 EnforceMaximumDeviation clamps and preserves when inside",
          "[unit][foundation][vector3]") {
  const Vector3 base{0.0f, 0.0f, 0.0f};
  const Vector3 far{10.0f, 0.0f, 0.0f};
  const Vector3 clamped = base.EnforceMaximumDeviation(far, 2.0f);
  // The result is moved toward `deviant`, so it stays maxDeviation away from it.
  CHECK(clamped.GetDistance(far) == Approx(2.0f).margin(1e-4f));
  CHECK(clamped.GetDistance(base) == Approx(8.0f).margin(1e-4f));

  const Vector3 near{1.0f, 0.0f, 0.0f};
  CHECK(base.EnforceMaximumDeviation(near, 2.0f) == base);
}

TEST_CASE("Vector3 scalar and vector arithmetic", "[unit][foundation][vector3]") {
  const Vector3 a{1.0f, 2.0f, 3.0f};
  const Vector3 b{2.0f, 2.0f, 2.0f};
  CHECK((a + b).coords[0] == Approx(3.0f));
  CHECK((a - b).coords[2] == Approx(1.0f));
  CHECK((a * 2.0f).coords[1] == Approx(4.0f));
  CHECK((a * b).coords[2] == Approx(6.0f));
  CHECK((-a).coords[0] == Approx(-1.0f));
}
