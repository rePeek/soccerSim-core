// Quaternion: geometric effect on vectors, not the internal four numbers.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "football/foundation/math/quaternion.hpp"
#include "football/foundation/math/vector3.hpp"

using football::math::Quaternion;
using football::math::Vector3;
using Catch::Approx;

namespace {

Quaternion QuarterTurnZ() {
  const float s = std::sin(football::math::pi * 0.25f);
  const float c = std::cos(football::math::pi * 0.25f);
  return Quaternion(0.0f, 0.0f, s, c);
}

Quaternion QuarterTurnY() {
  const float s = std::sin(football::math::pi * 0.25f);
  const float c = std::cos(football::math::pi * 0.25f);
  return Quaternion(0.0f, s, 0.0f, c);
}

void CheckVectorNear(const Vector3& actual, const Vector3& expected, float margin = 1e-5f) {
  CHECK(actual.coords[0] == Approx(expected.coords[0]).margin(margin));
  CHECK(actual.coords[1] == Approx(expected.coords[1]).margin(margin));
  CHECK(actual.coords[2] == Approx(expected.coords[2]).margin(margin));
}

}  // namespace

TEST_CASE("Quaternion identity leaves vectors unchanged", "[unit][foundation][quaternion]") {
  const Quaternion q;
  CheckVectorNear(q * Vector3(1.0f, -2.0f, 3.0f), Vector3(1.0f, -2.0f, 3.0f));
}

TEST_CASE("Quaternion quarter turn about z maps +x to +y", "[unit][foundation][quaternion]") {
  CheckVectorNear(QuarterTurnZ() * Vector3(1.0f, 0.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f));
}

TEST_CASE("Quaternion rotation preserves vector length", "[unit][foundation][quaternion]") {
  const Quaternion q = Quaternion(0.2f, 0.5f, -0.4f, 0.7f).GetNormalized();
  const Vector3 v{1.0f, 2.0f, -0.5f};
  const float len = v.GetLength();
  CHECK((q * v).GetLength() == Approx(len).margin(1e-5f));
}

TEST_CASE("Quaternion inverse undoes the rotation", "[unit][foundation][quaternion]") {
  const Quaternion q = QuarterTurnZ();
  const Vector3 v{0.4f, -1.3f, 2.0f};
  CheckVectorNear(q.GetInverse() * (q * v), v);
  CheckVectorNear(q * (q.GetInverse() * v), v);
}

TEST_CASE("Quaternion normalized has unit magnitude", "[unit][foundation][quaternion]") {
  const Quaternion q = Quaternion(1.0f, -2.0f, 3.0f, 4.0f).GetNormalized();
  CHECK(q.GetMagnitude() == Approx(1.0f).margin(1e-5f));
}

TEST_CASE("Quaternion slerp endpoints are exact", "[unit][foundation][quaternion]") {
  const Quaternion from;
  const Quaternion to = QuarterTurnZ();

  const Quaternion at0 = from.GetSlerped(0.0f, to);
  CHECK(at0.elements[0] == Approx(from.elements[0]).margin(1e-5f));
  CHECK(at0.elements[3] == Approx(from.elements[3]).margin(1e-5f));

  const Quaternion at1 = from.GetSlerped(1.0f, to);
  CHECK(at1.elements[2] == Approx(to.elements[2]).margin(1e-5f));
  CHECK(at1.elements[3] == Approx(to.elements[3]).margin(1e-5f));
}

TEST_CASE("Quaternion q and -q are the same rotation", "[unit][foundation][quaternion]") {
  const Quaternion q = Quaternion(0.2f, 0.5f, -0.4f, 0.7f).GetNormalized();
  const Vector3 v{1.0f, 2.0f, -0.5f};
  CheckVectorNear((-q) * v, q * v);
}

TEST_CASE("Quaternion composition matches sequential application",
          "[unit][foundation][quaternion]") {
  const Quaternion a = QuarterTurnZ();
  const Quaternion b = QuarterTurnY();
  const Vector3 v{1.0f, 0.0f, 0.0f};
  CheckVectorNear((a * b) * v, a * (b * v));
}
