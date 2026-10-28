#ifndef FOOTBALL_PLAYER_ACTIVE_IMPULSE_HPP
#define FOOTBALL_PLAYER_ACTIVE_IMPULSE_HPP

#include <algorithm>
#include <optional>
#include <span>

#include "football/ball/ball_impulse.hpp"
#include "football/player/state/player_definition.hpp"
#include "football/contact/player_body_collider_motion.hpp"
#include "football/player/state/player_action.hpp"

// P5b: deterministic arbitration of active endpoint impulses. Simulation owns
// the candidate list and the passive-impact facts; this header only selects at
// most one winner. Ball never sees player/action/rule identity.
struct ActiveImpulseCandidate {
  football::player::PlayerId player = football::player::kInvalidPlayerId;
  PlayerBodyPart body_part = PlayerBodyPart::LowerBody;
  e_FunctionType action = e_FunctionType_None;
  football::ball::BallImpulse impulse;
  // Magnitude of the required velocity change (|J| / mass). This is NOT the
  // relative closing speed along the contact normal; it measures how committed
  // the strike is and is the primary ordering key.
  float velocity_change_magnitude = 0.0f;
  // True when this tick's passive impact already belongs to the same
  // owner+part, so applying the active impulse too would double-resolve it.
  bool passive_same_part = false;
};

// Deterministic, stateless selection. Candidates suppressed by an existing
// same-part passive impact are dropped first; the remaining candidate with the
// largest closing speed wins, with lower PlayerId then lower body part as
// stable tie-breakers. Returns no impulse when nothing remains, so a tick can
// carry at most one active endpoint impulse.
std::optional<ActiveImpulseCandidate> ArbitrateActiveImpulse(
    std::span<const ActiveImpulseCandidate> candidates);

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

ContactAuthority DecideContactAuthority(const ContactAuthorityInput& input);

#endif
