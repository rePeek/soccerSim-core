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
  state.facing = Vector3(0.0f, -1.0f, 0.0f);
  state.bodyFacing = Vector3(0.0f, -1.0f, 0.0f);

  std::vector<Record> records;
  for (int i = 0; i < ticks; ++i) {
    const auto step =
        PlayerMotion::StepContinuous(state, Profile(), desired(i), idle(i), kDt);
    state = step.state;
    records.push_back({state.speed, state.velocity.GetLength(), HeadingDeg(state.facing)});
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

TEST_CASE("static start in a direction away from facing crosses the idle threshold") {
  const auto records = Run(200,
                           [](int) { return Vector3(1.0f, 0.0f, 0.0f) * 5.0f; },
                           [](int) { return Vector3(0.0f, -1.0f, 0.0f); });
  CheckSpeedClose(records);

  // Below the idle threshold the locomotion facing stays frozen at idleFacing,
  // then adopts the (already rate-limited) velocity direction once moving.
  int crossing = -1;
  for (std::size_t i = 0; i < records.size(); ++i) {
    if (records[i].speed > 0.5f) { crossing = static_cast<int>(i); break; }
  }
  REQUIRE(crossing > 0);
  REQUIRE(records[0].facing_deg == Catch::Approx(0.0f).margin(1e-3f));
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
