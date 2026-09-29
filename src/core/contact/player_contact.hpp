#ifndef _HPP_CORE_CONTACT_PLAYER_CONTACT
#define _HPP_CORE_CONTACT_PLAYER_CONTACT

#include <algorithm>
#include <cassert>
#include <cmath>
#include <optional>
#include <vector>

#include "core/contact/circle_contact.hpp"
#include "core/contact/player_collider.hpp"
#include "core/domain/player/player_profile.hpp"
#include "core/state/player_state.hpp"

namespace football::contact {

// 7G-2: player-player contact detector adapter.
//
// PlayerProfile + PlayerState -> BuildPlayerGroundCollider -> DetectContact.
// This is a pure core API: it knows nothing about PlayerBase, Match,
// possession, the referee, or GetStat(). Radius comes from the profile and the
// center from the state, both of which are simulation-owned.
inline std::optional<Contact> DetectPlayerContact(
    const football::domain::PlayerProfile &a_profile,
    const PlayerState &a_state,
    const football::domain::PlayerProfile &b_profile,
    const PlayerState &b_state) {
  DO_VALIDATION;
  return DetectContact(BuildPlayerGroundCollider(a_profile, a_state),
                       BuildPlayerGroundCollider(b_profile, b_state));
}

// 7G-3: the resolver returns a *result*, it does not mutate state.
//
//   State + Profile + Contact
//            |
//          Resolver
//            |
//       Resolution Result
//
// Callers decide when to commit (ApplyPlayerContactResolution or the batch
// solver below). This keeps detector ("what happened geometrically") separate
// from resolver ("how physical state responds").
struct PlayerContactResolution {
  blunted::Vector3 positionDeltaA = blunted::Vector3(0);
  blunted::Vector3 positionDeltaB = blunted::Vector3(0);

  blunted::Vector3 velocityDeltaA = blunted::Vector3(0);
  blunted::Vector3 velocityDeltaB = blunted::Vector3(0);
};

// 7G-4: first player-player constraint solver.
//
// It solves exactly two things, nothing more:
//   1. positional separation, distributed by inverse mass;
//   2. the inward normal velocity constraint (restitution == 0).
//
// Football players are not billiard balls: there is no bounce. `mass` drives
// the physical response; `strength` and `balance` are gameplay responses and
// are deliberately absent from this base solver.
inline PlayerContactResolution ResolvePlayerContact(
    const football::domain::PlayerProfile &a_profile,
    const PlayerState &a_state,
    const football::domain::PlayerProfile &b_profile,
    const PlayerState &b_state, const Contact &contact) {
  DO_VALIDATION;
  PlayerContactResolution result;

  // Dynamic bodies only. mass == 0 must not silently mean "infinite mass";
  // a future kinematic/static body should carry inverseMass = 0 explicitly.
  assert(std::isfinite(a_profile.physical.mass) && a_profile.physical.mass > 0.0f);
  assert(std::isfinite(b_profile.physical.mass) && b_profile.physical.mass > 0.0f);
  const float invA = 1.0f / a_profile.physical.mass;
  const float invB = 1.0f / b_profile.physical.mass;
  const float invSum = invA + invB;

  const float wA = invA / invSum;
  const float wB = invB / invSum;
  const blunted::Vector3 normal = contact.normal.Get2D();

  // Positional separation. A moves along -normal, B along +normal, each by its
  // inverse-mass share of the penetration.
  const float separation = contact.penetration;
  result.positionDeltaA = normal * (-separation * wA);
  result.positionDeltaB = normal * (separation * wB);
  result.positionDeltaA.coords[2] = 0.0f;
  result.positionDeltaB.coords[2] = 0.0f;

  // Inward normal velocity constraint. If the bodies are still approaching
  // along the contact normal, remove exactly that approach velocity (e == 0).
  const blunted::Vector3 relative =
      b_state.velocity.Get2D() - a_state.velocity.Get2D();
  const float relativeNormal = relative.GetDotProduct(normal);
  if (relativeNormal < 0.0f) {
    const float impulse = -relativeNormal / invSum;  // restitution == 0
    result.velocityDeltaA = normal * (-impulse * invA);
    result.velocityDeltaB = normal * (impulse * invB);
    result.velocityDeltaA.coords[2] = 0.0f;
    result.velocityDeltaB.coords[2] = 0.0f;
  }
  return result;
}

inline void ApplyPlayerContactResolution(PlayerState &a_state, PlayerState &b_state,
                                         const PlayerContactResolution &resolution) {
  DO_VALIDATION;
  a_state.position += resolution.positionDeltaA;
  b_state.position += resolution.positionDeltaB;
  a_state.velocity += resolution.velocityDeltaA;
  b_state.velocity += resolution.velocityDeltaB;
}

// 7G-6: a predicted batch of player states resolved together.
//
// The body holds a caller-provided stable index plus a snapshot of the
// profile/state pair. The solver:
//   1. sorts bodies by `index` (never by pointer address), so Replay,
//      training and regression stay bit-exact;
//   2. sweeps all unordered pairs in that order, detect -> resolve -> apply,
//      for a fixed number of iterations (Gauss-Seidel style).
//
// 22 players is 231 pairs; a brute-force broad phase is more than enough for
// the first version, so there is no spatial hash.
struct PlayerContactBody {
  int index = -1;  // caller-provided stable ordering key
  football::domain::PlayerProfile profile;
  PlayerState state;
};

struct PlayerContactSolverConfig {
  int iterations = 8;  // fixed, deterministic sweep count
};

inline void ResolvePlayerContactBatch(
    std::vector<PlayerContactBody> &bodies,
    const PlayerContactSolverConfig &config = PlayerContactSolverConfig{}) {
  DO_VALIDATION;
  assert(config.iterations > 0);
  // Stable indices are a solver precondition, not a regression oracle: a
  // forgotten or duplicated index silently makes Gauss-Seidel order depend on
  // the caller's vector layout, which would break Replay/dataset/self-play.
  for (const PlayerContactBody &body : bodies) assert(body.index >= 0);

  std::stable_sort(bodies.begin(), bodies.end(),
                   [](const PlayerContactBody &x, const PlayerContactBody &y) {
                     return x.index < y.index;
                   });

  for (std::size_t i = 1; i < bodies.size(); ++i) {
    assert(bodies[i - 1].index != bodies[i].index);
  }
  const int count = static_cast<int>(bodies.size());
  for (int iteration = 0; iteration < config.iterations; ++iteration) {
    DO_VALIDATION;
    for (int i = 0; i < count; ++i) {
      for (int j = i + 1; j < count; ++j) {
        const std::optional<Contact> contact =
            DetectContact(BuildPlayerGroundCollider(bodies[i].profile, bodies[i].state),
                          BuildPlayerGroundCollider(bodies[j].profile, bodies[j].state));
        if (!contact) continue;
        const PlayerContactResolution resolution =
            ResolvePlayerContact(bodies[i].profile, bodies[i].state,
                                 bodies[j].profile, bodies[j].state, *contact);
        ApplyPlayerContactResolution(bodies[i].state, bodies[j].state, resolution);
      }
    }
  }
}

}  // namespace football::contact

#endif  // _HPP_CORE_CONTACT_PLAYER_CONTACT