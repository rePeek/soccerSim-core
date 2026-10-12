// Scalar helpers: clamp semantics, angle wrapping and the easing curve.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "football/foundation/math/scalar.hpp"

using Catch::Approx;

TEST_CASE("scalar clamp bounds the value", "[unit][foundation][scalar]") {
  CHECK(blunted::clamp(5.0f, 0.0f, 10.0f) == Approx(5.0f));
  CHECK(blunted::clamp(-3.0f, 0.0f, 10.0f) == Approx(0.0f));
  CHECK(blunted::clamp(42.0f, 0.0f, 10.0f) == Approx(10.0f));
}

TEST_CASE("scalar NormalizedClamp maps onto [0, 1]", "[unit][foundation][scalar]") {
  CHECK(blunted::NormalizedClamp(5.0f, 0.0f, 10.0f) == Approx(0.5f));
  CHECK(blunted::NormalizedClamp(-5.0f, 0.0f, 10.0f) == Approx(0.0f));
  CHECK(blunted::NormalizedClamp(50.0f, 0.0f, 10.0f) == Approx(1.0f));
}

TEST_CASE("scalar ModulateIntoRange wraps into the interval",
          "[unit][foundation][scalar]") {
  CHECK(float(blunted::ModulateIntoRange(0.0f, 10.0f, blunted::radian(15.0f))) == Approx(5.0f));
  CHECK(float(blunted::ModulateIntoRange(0.0f, 10.0f, blunted::radian(-3.0f))) == Approx(7.0f));
  CHECK(float(blunted::ModulateIntoRange(0.0f, 10.0f, blunted::radian(4.0f))) == Approx(4.0f));
}

TEST_CASE("scalar curve is a smooth identity at its endpoints",
          "[unit][foundation][scalar]") {
  CHECK(blunted::curve(0.0f) == Approx(0.0f).margin(1e-6f));
  CHECK(blunted::curve(0.5f) == Approx(0.5f).margin(1e-6f));
  CHECK(blunted::curve(1.0f) == Approx(1.0f).margin(1e-6f));
}

TEST_CASE("scalar signSide and is_odd", "[unit][foundation][scalar]") {
  CHECK(blunted::signSide(3.0f) == 1);
  CHECK(blunted::signSide(-3.0f) == -1);
  CHECK(blunted::signSide(0.0f) == 1);
  CHECK(blunted::is_odd(3));
  CHECK_FALSE(blunted::is_odd(4));
}
