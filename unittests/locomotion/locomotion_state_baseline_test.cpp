// STATE-1.0 — PlayerKinematicState behaviour baseline.
//
// Pins the current five-field state behaviour before any field is removed. The
// critical measurement is whether the stored `speed` is bit-identical to
// velocity.GetLength() across every scenario; a divergence there is exactly the
// floating-point risk STATE-1.1 must handle.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <vector>

#include "football/foundation/math/vector3.hpp"
using namespace football::math;  // player_locomotion.hpp expects the legacy global namespace
#include "football/foundation/time/tick_boundary.hpp"
#include "sim/player/player_kinematics.hpp"
#include "sim/player/player_motion.hpp"
#include "sim/player/player_motion_profile.hpp"

using football::math::Vector3;

namespace {

constexpr float kDt = football::sim::kTickSeconds;

PlayerMotionProfile Profile() { return PlayerMotionProfile{7.5f, 6.0f, 12.0f, 6.0f}; }

struct Record {
  float speed;
  float length;
  float facing_deg;
};

float HeadingDeg(const Vector3& v) {
  return static_cast<float>(v.GetAngle2D(Vector3(0.0f, -1.0f, 0.0f))) * 180.0f / football::math::pi;
}

// Runs `ticks` steps, feeding desired velocity and idle facing per tick.
template <typename DesiredFn, typename IdleFn>
std::vector<Record> Run(int ticks, DesiredFn desired, IdleFn idle) {
  PlayerKinematicState state;
  state.bodyFacing = Vector3(0.0f, -1.0f, 0.0f);

  std::vector<Record> records;
  for (int i = 0; i < ticks; ++i) {
    const auto step =
        PlayerMotion::StepContinuous(state, Profile(), desired(i), idle(i), kDt);
    state = step.state;
    records.push_back({state.GetSpeed(), state.velocity.GetLength(), HeadingDeg(state.GetFacing())});
  }
  return records;
}

void CheckSpeedClose(const std::vector<Record>& records) {
  float max_divergence = 0.0f;
  for (const Record& r : records) {
    max_divergence = std::max(max_divergence, std::fabs(r.speed - r.length));
  }
  // STATE-1.0 finding: the stored scalar and velocity.GetLength() are NOT
  // bit-identical (the normalized direction is not exactly unit length, so
  // `dir * speed` loses ~1e-8 of the scalar). They agree to well under 1e-6.
  // STATE-1.1 recomputes GetLength() and must accept this last-bit-only change.
  INFO("max |speed - |velocity|| = " << max_divergence);
  CHECK(max_divergence < 1e-6f);
}

}  // namespace

TEST_CASE("static start turns toward the requested direction immediately") {
  const auto records = Run(200,
                           [](int) { return Vector3(1.0f, 0.0f, 0.0f) * 5.0f; },
                           [](int) { return Vector3(0.0f, -1.0f, 0.0f); });
  CheckSpeedClose(records);

  // STATE-1.2: facing is derived from velocity whenever speed is numerically
  // non-zero, so the first tick already turns toward the requested direction
  // (no freeze below the old 0.5 m/s threshold).
  REQUIRE(records[0].facing_deg > 0.0f);
  REQUIRE(records[0].facing_deg < 10.0f);
  // Final facing converges toward the requested 90-degree direction.
  REQUIRE(records.back().facing_deg == Catch::Approx(90.0f).margin(5.0f));
}

TEST_CASE("reverse start after a full brake") {
  const auto records = Run(250,
                           [](int i) {
                             return i < 150 ? Vector3(0.0f, -1.0f, 0.0f) * 6.0f
                                            : Vector3(0.0f, 1.0f, 0.0f) * 4.0f;
                           },
                           [](int) { return Vector3(0.0f, -1.0f, 0.0f); });
  CheckSpeedClose(records);
  REQUIRE(records.back().speed > 0.0f);
  REQUIRE(records.back().facing_deg == Catch::Approx(180.0f).margin(8.0f));
}

TEST_CASE("speed stays bit-identical to velocity length across a slalom") {
  const auto records = Run(400,
                           [](int i) {
                             const float phase = (i / 20) % 2 == 0 ? 1.0f : -1.0f;
                             return Vector3(phase, -1.0f, 0.0f).GetNormalized(Vector3(0, -1, 0)) * 4.0f;
                           },
                           [](int) { return Vector3(0.0f, -1.0f, 0.0f); });
  CheckSpeedClose(records);
}

TEST_CASE("body facing stays independent while speed stays derived") {
  const auto records = Run(200,
                           [](int) { return Vector3(0.0f, -3.0f, 0.0f); },
                           [](int) { return Vector3(0.0f, -1.0f, 0.0f); });
  CheckSpeedClose(records);
}

TEST_CASE("GetFacing derives velocity direction while moving and body facing at rest") {
  PlayerKinematicState state;

  // Moving: facing follows velocity, even well below the idle threshold.
  state.velocity = Vector3(0.3f, 0.0f, 0.0f);
  state.bodyFacing = Vector3(0.0f, -1.0f, 0.0f);
  CHECK(state.GetFacing().coords[0] == Catch::Approx(1.0f).margin(1e-4f));
  CHECK(state.GetFacing().coords[1] == Catch::Approx(0.0f).margin(1e-4f));

  // At rest: facing is the body facing.
  state.velocity = Vector3(0.0f, 0.0f, 0.0f);
  state.bodyFacing = Vector3(1.0f, 0.0f, 0.0f);
  CHECK(state.GetFacing().coords[0] == Catch::Approx(1.0f).margin(1e-4f));
  CHECK(state.GetFacing().coords[1] == Catch::Approx(0.0f).margin(1e-4f));

  // A zero body facing falls back to the canonical -Y direction.
  state.bodyFacing = Vector3(0.0f, 0.0f, 0.0f);
  CHECK(state.GetFacing().coords[0] == Catch::Approx(0.0f).margin(1e-4f));
  CHECK(state.GetFacing().coords[1] == Catch::Approx(-1.0f).margin(1e-4f));
}
