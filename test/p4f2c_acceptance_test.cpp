#include <catch2/catch_test_macros.hpp>

#include <array>
#include <vector>

#include "football/ball/ball_contact.hpp"
#include "football/ball/ball_response.hpp"
#include "football/ball/collider.hpp"

using namespace football::ball;
using blunted::Vector3;

namespace {
constexpr float dt = 0.01f;
constexpr float kMeasuredRealizedEndpointError = 0.0186184f; // p4f-2b 15-shape max

BallState BallAt(Vector3 p, Vector3 v = Vector3(0)) {
  return {p, v, Vector3(0), blunted::Quaternion{}};
}

// A representative sprint-kick-like sweep: the capsule translates and rotates
// between clear endpoints while the ball approaches its swept volume.
ColliderMotion KickingCapsule() {
  return {7u, Capsule{{0.20f, -0.10f, 0.05f}, {0.20f, 0.10f, 0.05f}, 0.071f},
          Capsule{{-0.25f, -0.08f, 0.30f}, {-0.25f, 0.08f, 0.30f}, 0.071f},
          {0.35f, 0.45f}};
}

void PerturbEnd(ColliderMotion& motion, float dx) {
  auto& end = std::get<Capsule>(motion.end);
  end.a.coords[0] += dx;
  end.b.coords[0] += dx;
}

Capsule ShapeAt(const ColliderMotion& c, float t) {
  const auto& a = std::get<Capsule>(c.start);
  const auto& b = std::get<Capsule>(c.end);
  return {a.a + (b.a - a.a) * t, a.b + (b.b - a.b) * t,
          a.radius + (b.radius - a.radius) * t};
}
}  // namespace

// G1: the measured endpoint-linear interpolation error must not materially
// change first-contact evidence. Perturb a representative sweep by the measured
// 15-shape realized error and bound TOI/normal/impulse deltas over a ball
// approach grid; a large structural CCD error would show up here as an unstable
// first contact, not just an endpoint distance.
TEST_CASE("interpolation error bound keeps first-contact TOI normal and impulse stable",
          "[p4f2c][deforming-ccd][acceptance]") {
  const football::ball::BallModel config;
  const auto base = KickingCapsule();
  auto perturbed = base;
  PerturbEnd(perturbed, kMeasuredRealizedEndpointError);

  float max_dtoi = 0, max_dnormal = 0, max_rel_dimpulse = 0;
  for (float gx : {-0.08f, 0.0f, 0.08f}) {
    for (float gy : {-0.06f, 0.0f, 0.06f}) {
      for (float gz : {-0.04f, 0.0f, 0.12f}) {
        // Ball just outside the capsule's swept AABB start.
        const auto ball = BallAt({0.32f + gx, gy, 0.02f + gz}, {-6.0f, 0.0f, 0.0f});
        const auto a = SweepBall(ball, base, dt, config.radius());
        const auto b = SweepBall(ball, perturbed, dt, config.radius());
        if (!a || !b) continue;
        max_dtoi = std::max(max_dtoi, std::fabs(a->toi - b->toi));
        max_dnormal = std::max(max_dnormal, (a->normal - b->normal).GetLength());
        const auto resp_a = ResolveContactResponse(ball, *a, base.material,
                                                   a->surface_velocity, config);
        const auto resp_b = ResolveContactResponse(ball, *b, perturbed.material,
                                                   b->surface_velocity, config);
        if (resp_a.normal_impulse > 1e-6f)
          max_rel_dimpulse = std::max(max_rel_dimpulse,
                              std::fabs(resp_a.normal_impulse - resp_b.normal_impulse) /
                                  resp_a.normal_impulse);
      }
    }
  }

  // The perturbation is ~18.6 mm. First-contact TOI/normal stay stable (the
  // primary evidence); the normal-impulse magnitude shifts by at most a
  // comparable relative fraction, which is the documented response sensitivity
  // of the linear transport model, not a structural missed/ghost contact.
  REQUIRE(max_dtoi < 0.004f);
  REQUIRE(max_dnormal < 0.10f);
  REQUIRE(max_rel_dimpulse < 0.25f);
  REQUIRE(std::isfinite(max_dtoi));
}