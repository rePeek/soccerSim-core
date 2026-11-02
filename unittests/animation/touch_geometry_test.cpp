#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "football/animation/touch_geometry.hpp"

using Catch::Approx;
using football::animation::SegmentProjectionParameter;
using football::math::Vector3;

TEST_CASE("segment projection keeps the legacy 2D/3D formula") {
  CHECK(SegmentProjectionParameter(Vector3(0, 0, 0), Vector3(10, 0, 0), Vector3(5, 5, 0)) ==
        Approx(0.5f));
  CHECK(SegmentProjectionParameter(Vector3(1, 1, 0), Vector3(1, 1, 0), Vector3(4, 4, 0)) ==
        Approx(0.0f));
  // The Z component contributes to length, but never to the numerator.
  CHECK(SegmentProjectionParameter(Vector3(0), Vector3(3, 0, 4), Vector3(3, 0, 4)) ==
        Approx(9.0f / 25.0f));
  CHECK(SegmentProjectionParameter(Vector3(0), Vector3(1, 0, 0), Vector3(2, 0, 0)) ==
        Approx(2.0f));
}
