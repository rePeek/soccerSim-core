#ifndef FOOTBALL_PLAYER_ACTIVE_IMPULSE_HPP
#define FOOTBALL_PLAYER_ACTIVE_IMPULSE_HPP

#include <algorithm>
#include <optional>
#include <span>

#include "football/ball/ball_impulse.hpp"
#include "model/player.hpp"
#include "sim/player/player_body_collider_motion.hpp"
#include "sim/player/player_action.hpp"

// P5b: deterministic arbitration of active endpoint impulses. Simulation owns
// the candidate list and the passive-impact facts; this header only selects at
// most one winner. Ball never sees player/action/rule identity.
struct ActiveImpulseCandidate {
  football::model::PlayerId player = football::model::kInvalidPlayerId;
  PlayerBodyPart body_part = PlayerBodyPart::LowerBody;
  e_FunctionType action = e_FunctionType_None;
  football::ball::BallImpulse impulse;
  // Relative approach speed along the contact normal at the contact instant.
  // Larger means a more committed contact; used as the primary ordering key.
  float closing_speed = 0.0f;
  // True when this tick's passive impact already belongs to the same
  // owner+part, so applying the active impulse too would double-resolve it.
  bool passive_same_part = false;
};

// Deterministic, stateless selection. Candidates suppressed by an existing
// same-part passive impact are dropped first; the remaining candidate with the
// largest closing speed wins, with lower PlayerId then lower body part as
// stable tie-breakers. Returns no impulse when nothing remains, so a tick can
// carry at most one active endpoint impulse.
inline std::optional<ActiveImpulseCandidate> ArbitrateActiveImpulse(
    std::span<const ActiveImpulseCandidate> candidates) {
  std::optional<ActiveImpulseCandidate> winner;
  for (const ActiveImpulseCandidate& candidate : candidates) {
    if (candidate.passive_same_part) continue;
    if (!winner.has_value() || candidate.closing_speed > winner->closing_speed ||
        (candidate.closing_speed == winner->closing_speed &&
         (candidate.player < winner->player ||
          (candidate.player == winner->player &&
           candidate.body_part < winner->body_part)))) {
      winner = candidate;
    }
  }
  return winner;
}

// P4d-2 prerequisite: who owns this tick's ball contact. A *real* passive body
// impact (nonzero normal impulse) precedes an active strike, whether it is the
// same owner+part (no double resolution) or another player (they got there
// first). A zero-impulse overlap projection is NOT an impact and must never
// suppress a valid active strike.
enum class ContactAuthority { PassiveImpact, ActiveTouch, None };

struct ContactAuthorityInput {
  bool passive_impact_exists = false;   // earliest passive contact has normal_impulse > 0
  bool passive_same_owner_part = false; // that impact belongs to the active candidate's owner+part
  bool active_candidate_valid = false;
};

inline ContactAuthority DecideContactAuthority(const ContactAuthorityInput& input) {
  if (input.passive_impact_exists) return ContactAuthority::PassiveImpact;
  if (input.active_candidate_valid) return ContactAuthority::ActiveTouch;
  return ContactAuthority::None;
}

#endif
