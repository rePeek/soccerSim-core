#include "football/contact/player_active_impulse.hpp"

std::optional<ActiveImpulseCandidate> ArbitrateActiveImpulse(
    std::span<const ActiveImpulseCandidate> candidates) {
  std::optional<ActiveImpulseCandidate> winner;
  for (const ActiveImpulseCandidate& candidate : candidates) {
    if (candidate.passive_same_part) continue;
    if (!winner.has_value() || candidate.velocity_change_magnitude > winner->velocity_change_magnitude ||
        (candidate.velocity_change_magnitude == winner->velocity_change_magnitude &&
         (candidate.player < winner->player ||
          (candidate.player == winner->player &&
           candidate.body_part < winner->body_part)))) {
      winner = candidate;
    }
  }
  return winner;
}

ContactAuthority DecideContactAuthority(const ContactAuthorityInput& input) {
  if (input.passive_impact_exists) return ContactAuthority::PassiveImpact;
  if (input.active_candidate_valid) return ContactAuthority::ActiveTouch;
  return ContactAuthority::None;
}
