// LOCO-2.1 — off-ball locomotion physics baseline.
//
// Drives the real PlayerMotion::StepContinuous + PlayerBodyFacing at a fixed
// 100 Hz, without a Simulation, animation asset, Humanoid or RNG. It pins the
// physics contracts (speed/acceleration/braking/turn-rate bounds, no NaN, no
// position jumps, determinism) and provides a bounded tick-by-tick diagnostic.
//
// Turn rate is measured on the *velocity direction*: the locomotion facing
// field is intentionally frozen to idleFacing below the idle threshold and then
// adopts the (already rate-limited) velocity direction once moving, so facing
// alone is not a rate-limited trajectory.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "football/foundation/math/vector3.hpp"
using namespace football::math;  // player_body_facing.hpp expects the legacy global namespace
#include "football/foundation/time/tick_boundary.hpp"
#include "sim/player/player_body_facing.hpp"
#include "sim/player/player_kinematics.hpp"
#include "sim/player/player_motion.hpp"
#include "sim/player/player_motion_profile.hpp"

using football::math::Vector3;

namespace {

constexpr float kDt = football::sim::kTickSeconds;  // 10 ms at 100 Hz
constexpr float kEps = 1e-3f;

PlayerMotionProfile DefaultProfile() { return PlayerMotionProfile{7.5f, 6.0f, 12.0f, 6.0f}; }
PlayerMotionProfile WeakerProfile() { return PlayerMotionProfile{6.0f, 4.0f, 10.0f, 5.0f}; }

struct TickRecord {
  float t;
  float x, y;
  float speed;
  float acceleration;
  float facing_deg;
  float body_deg;
  float turn_rate;
};

float HeadingDeg(const Vector3& v) {
  return static_cast<float>(v.GetAngle2D(Vector3(0.0f, -1.0f, 0.0f))) * 180.0f / football::math::pi;
}

template <typename DesiredFn, typename BodyFn>
std::vector<TickRecord> Run(const PlayerMotionProfile& profile, int ticks,
                            DesiredFn desired, BodyFn body) {
  PlayerKinematicState state;
  state.bodyFacing = Vector3(0.0f, -1.0f, 0.0f);

  std::vector<TickRecord> records;
  records.reserve(static_cast<std::size_t>(ticks));
  float previous_speed = 0.0f;
  Vector3 previous_velocity_dir = state.GetFacing();

  for (int i = 0; i < ticks; ++i) {
    const Vector3 desired_velocity = desired(i);
    const auto step = PlayerMotion::StepContinuous(state, profile, desired_velocity, Vector3(0.0f, -1.0f, 0.0f), kDt);
    state = step.state;

    PlayerBodyFacingInput body_input;
    body_input.desiredFacing = body(i, state);
    PlayerBodyFacing::Step(state, body_input, PlayerBodyFacingParameters{}, kDt);

    const float speed = state.velocity.GetLength();
    const Vector3 velocity_dir = state.velocity.GetNormalized(previous_velocity_dir);
    const float turn = (speed > 1e-3f && previous_speed > 1e-3f)
                           ? std::fabs(static_cast<float>(previous_velocity_dir.GetAngle2D(velocity_dir)))
                           : 0.0f;

    TickRecord r;
    r.t = static_cast<float>(i + 1) * kDt;
    r.x = state.position.coords[0];
    r.y = state.position.coords[1];
    r.speed = speed;
    r.acceleration = (speed - previous_speed) / kDt;
    r.facing_deg = HeadingDeg(state.GetFacing());
    r.body_deg = HeadingDeg(state.bodyFacing);
    r.turn_rate = turn / kDt;
    records.push_back(r);

    previous_speed = speed;
    previous_velocity_dir = velocity_dir;
  }
  return records;
}

void DumpRecords(const char* name, const std::vector<TickRecord>& records) {
  if (std::getenv("FOOTBALL_LOCO_DIAG") == nullptr) return;
  std::fprintf(stderr, "scenario=%s\n", name);
  std::fprintf(stderr, "tick,t,x,y,speed,accel,facing_deg,body_deg,turn_rate\n");
  for (std::size_t i = 0; i < records.size(); ++i) {
    const auto& r = records[i];
    std::fprintf(stderr, "%zu,%.3f,%.3f,%.3f,%.3f,%+.3f,%.2f,%.2f,%.2f\n",
                 i, r.t, r.x, r.y, r.speed, r.acceleration, r.facing_deg,
                 r.body_deg, r.turn_rate);
  }
}

void CheckPhysicsContracts(const std::vector<TickRecord>& records,
                           const PlayerMotionProfile& profile) {
  for (std::size_t i = 0; i < records.size(); ++i) {
    const auto& r = records[i];
    REQUIRE(std::isfinite(r.x));
    REQUIRE(std::isfinite(r.y));
    REQUIRE(std::isfinite(r.speed));
    REQUIRE(std::isfinite(r.acceleration));
    REQUIRE(std::isfinite(r.turn_rate));
    REQUIRE(r.speed <= profile.max_speed + kEps);
    REQUIRE(r.speed >= -kEps);
    REQUIRE(r.acceleration <= profile.acceleration + kEps);
    REQUIRE(r.acceleration >= -profile.braking - kEps);
    REQUIRE(r.turn_rate <= profile.turn_rate + kEps);
    if (i > 0) {
      const float dx = records[i].x - records[i - 1].x;
      const float dy = records[i].y - records[i - 1].y;
      REQUIRE(std::sqrt(dx * dx + dy * dy) <= profile.max_speed * kDt + kEps);
    }
  }
}

}  // namespace

TEST_CASE("stand still for one second") {
  const auto records = Run(DefaultProfile(), 100,
                           [](int) { return Vector3(0.0f, 0.0f, 0.0f); },
                           [](int, const PlayerKinematicState&) { return Vector3(0.0f, -1.0f, 0.0f); });
  DumpRecords("stand", records);
  CheckPhysicsContracts(records, DefaultProfile());
  REQUIRE(records.back().speed == Catch::Approx(0.0f).margin(kEps));
  REQUIRE(records.back().x == Catch::Approx(0.0f).margin(kEps));
  REQUIRE(records.back().y == Catch::Approx(0.0f).margin(kEps));
}

TEST_CASE("accelerate from rest to max speed") {
  const auto records = Run(DefaultProfile(), 250,
                           [](int) { return Vector3(0.0f, -7.5f, 0.0f); },
                           [](int, const PlayerKinematicState&) { return Vector3(0.0f, -1.0f, 0.0f); });
  DumpRecords("accel", records);
  CheckPhysicsContracts(records, DefaultProfile());
  REQUIRE(records.back().speed == Catch::Approx(DefaultProfile().max_speed).margin(0.01f));
  bool monotonic = true;
  for (std::size_t i = 1; i < records.size(); ++i)
    if (records[i].speed < records[i - 1].speed - kEps) monotonic = false;
  REQUIRE(monotonic);
}

TEST_CASE("low and high speed cruise hold their target") {
  for (const float target : {2.0f, 6.0f}) {
    const auto records = Run(DefaultProfile(), 300,
                             [target](int) { return Vector3(0.0f, -target, 0.0f); },
                             [](int, const PlayerKinematicState&) { return Vector3(0.0f, -1.0f, 0.0f); });
    DumpRecords(target < 3.0f ? "cruise2" : "cruise6", records);
    CheckPhysicsContracts(records, DefaultProfile());
    REQUIRE(records.back().speed == Catch::Approx(target).margin(0.01f));
  }
}

TEST_CASE("brake from high speed to rest") {
  // First 150 ticks accelerate to ~6 m/s, then brake to a stop.
  const auto brake = Run(DefaultProfile(), 250,
                         [](int i) {
                           return i < 150 ? Vector3(0.0f, -6.0f, 0.0f) : Vector3(0.0f, 0.0f, 0.0f);
                         },
                         [](int, const PlayerKinematicState&) { return Vector3(0.0f, -1.0f, 0.0f); });
  DumpRecords("brake", brake);
  CheckPhysicsContracts(brake, DefaultProfile());
  float peak = 0.0f;
  for (const auto& r : brake) peak = std::max(peak, r.speed);
  REQUIRE(peak == Catch::Approx(6.0f).margin(0.05f));
  REQUIRE(brake.back().speed == Catch::Approx(0.0f).margin(0.01f));
}

TEST_CASE("high speed 45 and 90 degree turns are rate limited") {
  for (const float angle_deg : {45.0f, 90.0f}) {
    const auto records = Run(DefaultProfile(), 300,
                             [angle_deg](int) {
                               const auto dir = Vector3(0.0f, -1.0f, 0.0f)
                                                    .GetRotated2D(angle_deg * football::math::pi / 180.0f);
                               return dir * 5.0f;
                             },
                             [](int, const PlayerKinematicState&) { return Vector3(0.0f, -1.0f, 0.0f); });
    DumpRecords(angle_deg > 45.0f ? "turn90" : "turn45", records);
    CheckPhysicsContracts(records, DefaultProfile());
    REQUIRE(records.back().speed > 0.0f);
    // Settled heading matches the requested turn (body direction slightly lags).
    REQUIRE(records.back().facing_deg == Catch::Approx(angle_deg).margin(5.0f));
  }
}

TEST_CASE("180 degree turnaround is a single bounded maneuver") {
  const auto records = Run(DefaultProfile(), 300,
                           [](int i) {
                             return (i < 150) ? Vector3(0.0f, -1.0f, 0.0f) * 6.0f
                                              : Vector3(0.0f, 1.0f, 0.0f) * 6.0f;
                           },
                           [](int, const PlayerKinematicState&) { return Vector3(0.0f, -1.0f, 0.0f); });
  DumpRecords("turn180", records);
  CheckPhysicsContracts(records, DefaultProfile());
  REQUIRE(records.back().facing_deg == Catch::Approx(180.0f).margin(8.0f));
}

TEST_CASE("continuous side to side slalom stays bounded") {
  const auto records = Run(DefaultProfile(), 400,
                           [](int i) {
                             const float phase = (i / 20) % 2 == 0 ? 1.0f : -1.0f;
                             return Vector3(phase, -1.0f, 0.0f).GetNormalized(Vector3(0, -1, 0)) * 4.0f;
                           },
                           [](int, const PlayerKinematicState&) { return Vector3(0.0f, -1.0f, 0.0f); });
  DumpRecords("slalom", records);
  CheckPhysicsContracts(records, DefaultProfile());
  REQUIRE(records.back().speed > 0.0f);
}

TEST_CASE("body observing direction is independent and cone clamped") {
  const auto records = Run(DefaultProfile(), 200,
                           [](int) { return Vector3(0.0f, -3.0f, 0.0f); },
                           [](int, const PlayerKinematicState& s) {
                             const auto fwd = s.GetFacing().GetNormalized(Vector3(0, -1, 0));
                             return fwd.GetRotated2D(90.0f * football::math::pi / 180.0f);
                           });
  DumpRecords("look_side", records);
  CheckPhysicsContracts(records, DefaultProfile());
  REQUIRE(records.back().body_deg > 45.0f);
  REQUIRE(records.back().body_deg < 135.0f);
}

TEST_CASE("same input replays bit-identically") {
  const auto once = Run(DefaultProfile(), 200,
                        [](int i) { return Vector3((i % 3 == 0) ? 1.0f : 0.0f, -3.0f, 0.0f); },
                        [](int, const PlayerKinematicState&) { return Vector3(0.0f, -1.0f, 0.0f); });
  const auto twice = Run(DefaultProfile(), 200,
                         [](int i) { return Vector3((i % 3 == 0) ? 1.0f : 0.0f, -3.0f, 0.0f); },
                         [](int, const PlayerKinematicState&) { return Vector3(0.0f, -1.0f, 0.0f); });
  REQUIRE(once.size() == twice.size());
  for (std::size_t i = 0; i < once.size(); ++i) {
    REQUIRE(once[i].x == twice[i].x);
    REQUIRE(once[i].y == twice[i].y);
    REQUIRE(once[i].speed == twice[i].speed);
  }
}

TEST_CASE("different motion capability produces different trajectories") {
  const auto fast = Run(DefaultProfile(), 150,
                        [](int) { return Vector3(0.0f, -7.5f, 0.0f); },
                        [](int, const PlayerKinematicState&) { return Vector3(0.0f, -1.0f, 0.0f); });
  const auto slow = Run(WeakerProfile(), 150,
                        [](int) { return Vector3(0.0f, -6.0f, 0.0f); },
                        [](int, const PlayerKinematicState&) { return Vector3(0.0f, -1.0f, 0.0f); });
  CheckPhysicsContracts(fast, DefaultProfile());
  CheckPhysicsContracts(slow, WeakerProfile());
  REQUIRE(std::fabs(fast.back().y) > std::fabs(slow.back().y));
}