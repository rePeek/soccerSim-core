#ifndef FOOTBALL_CORE_CONTACT_PLAYER_CONTACT_HPP
#define FOOTBALL_CORE_CONTACT_PLAYER_CONTACT_HPP

#include <algorithm>
#include <cassert>
#include <cmath>
#include <functional>
#include <optional>
#include <vector>

#include "core/physics/contact/geometry/circle_contact.hpp"
#include "core/physics/contact/player/player_collider.hpp"
#include "core/model/player/player.hpp"

namespace football_sim::contact {

inline std::optional<Contact> DetectPlayerContact(
    const football_sim::Player& a, const football_sim::Player& b) {
  return DetectContact(BuildPlayerGroundCollider(a), BuildPlayerGroundCollider(b));
}

struct PlayerContactResolution {
  football_sim::math::Vector3 positionDeltaA = football_sim::math::Vector3(0.0f, 0.0f, 0.0f);
  football_sim::math::Vector3 positionDeltaB = football_sim::math::Vector3(0.0f, 0.0f, 0.0f);
  football_sim::math::Vector3 velocityDeltaA = football_sim::math::Vector3(0.0f, 0.0f, 0.0f);
  football_sim::math::Vector3 velocityDeltaB = football_sim::math::Vector3(0.0f, 0.0f, 0.0f);
};

// Football players are non-bouncing dynamic bodies: solve positional overlap
// and inward normal velocity only. Strength/balance remain gameplay inputs,
// deliberately outside this base physical constraint.
inline PlayerContactResolution ResolvePlayerContact(
    const football_sim::Player& a, const football_sim::Player& b,
    const Contact& contact) {
  assert(std::isfinite(a.Mass()) && a.Mass() > 0.0f);
  assert(std::isfinite(b.Mass()) && b.Mass() > 0.0f);

  PlayerContactResolution result;
  const float inverseMassA = 1.0f / a.Mass();
  const float inverseMassB = 1.0f / b.Mass();
  const float inverseMassSum = inverseMassA + inverseMassB;
  const float weightA = inverseMassA / inverseMassSum;
  const float weightB = inverseMassB / inverseMassSum;
  const football_sim::math::Vector3 normal = contact.normal.Get2D();

  result.positionDeltaA = normal * (-contact.penetration * weightA);
  result.positionDeltaB = normal * (contact.penetration * weightB);
  result.positionDeltaA.coords[2] = 0.0f;
  result.positionDeltaB.coords[2] = 0.0f;

  const football_sim::math::Vector3 relativeVelocity =
      b.Velocity().Get2D() - a.Velocity().Get2D();
  const float relativeNormal = relativeVelocity.GetDotProduct(normal);
  if (relativeNormal < 0.0f) {
    const float impulse = -relativeNormal / inverseMassSum;
    result.velocityDeltaA = normal * (-impulse * inverseMassA);
    result.velocityDeltaB = normal * (impulse * inverseMassB);
    result.velocityDeltaA.coords[2] = 0.0f;
    result.velocityDeltaB.coords[2] = 0.0f;
  }
  return result;
}

inline void ApplyPlayerContactResolution(
    football_sim::Player& a, football_sim::Player& b,
    const PlayerContactResolution& resolution) {
  football_sim::PlayerState nextA = a.State();
  football_sim::PlayerState nextB = b.State();
  nextA.position += resolution.positionDeltaA;
  nextB.position += resolution.positionDeltaB;
  nextA.velocity += resolution.velocityDeltaA;
  nextB.velocity += resolution.velocityDeltaB;
  a.SetState(nextA);
  b.SetState(nextB);
}

struct PlayerContactSolverConfig {
  int iterations = 8;
};

// Stable PlayerId, rather than vector position or pointer address, defines
// deterministic Gauss-Seidel pair order.
inline void ResolvePlayerContactBatch(
    std::vector<std::reference_wrapper<football_sim::Player>>& players,
    const PlayerContactSolverConfig& config = {}) {
  assert(config.iterations > 0);
  std::stable_sort(
      players.begin(), players.end(),
      [](const auto& left, const auto& right) { return left.get().Id() < right.get().Id(); });
  for (std::size_t i = 1; i < players.size(); ++i) {
    assert(players[i - 1].get().Id() != players[i].get().Id());
  }

  for (int iteration = 0; iteration < config.iterations; ++iteration) {
    for (std::size_t i = 0; i < players.size(); ++i) {
      for (std::size_t j = i + 1; j < players.size(); ++j) {
        football_sim::Player& a = players[i].get();
        football_sim::Player& b = players[j].get();
        const std::optional<Contact> contact = DetectPlayerContact(a, b);
        if (!contact) continue;
        const PlayerContactResolution resolution =
            ResolvePlayerContact(a, b, *contact);
        ApplyPlayerContactResolution(a, b, resolution);
      }
    }
  }
}

}  // namespace football_sim::contact

#endif  // FOOTBALL_CORE_CONTACT_PLAYER_CONTACT_HPP
