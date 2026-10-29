// Player-side diagnostics that are still reachable from gameplay code.
//
// Everything here is measurement or an explicitly disabled A/B hook: it never
// decides a selection, a motion or a contact. The historical migration
// counters (Humanoid/PlayerPath/LegacyOnlyDecision/..., which nothing read) were
// removed with their writers; a diagnostic that has no consumer is not kept.
//
// Storage scope: the remaining entries are process-global single instances.
// They aggregate all matches in the process, so a future parallel-match runner
// must not share them across simulations.
#ifndef FOOTBALL_SIM_PLAYER_PLAYER_DIAGNOSTICS_HPP
#define FOOTBALL_SIM_PLAYER_PLAYER_DIAGNOSTICS_HPP

#include <vector>

#include "football/animation/types.hpp"
#include "football/player/player_definition.hpp"

// 4f-c diagnostic A/B hook. Transient and disabled in normal gameplay; a
// restored branch can choose the alternative Movement foot order exactly once.
struct MovementAnimationPerturbation {
  bool enabled = false;
  bool require_frame_count_difference = false;
  bool applied = false;
  football::player::PlayerId player_id = football::player::kInvalidPlayerId;
  int time_ms = -1;
  int original_anim_id = -1;
  int alternative_anim_id = -1;
};
MovementAnimationPerturbation &MovementAnimationPerturbationAudit();

// 5a1: transient observation, never used to decide contact or serialized.
// A football::ball::Ball::Touch request is not proof that football::ball::Ball
// physics adopted that impulse.
struct ContactAuthorityAudit {
  int scheduled = 0;
  int at_contact_frame = 0;
  int distance_rejected = 0;
  int height_rejected = 0;
  int physically_reachable = 0;
  int impulse_calls = 0;
  int nonzero_impulse_requests = 0;
  int suppressed = 0;
  int reachable_without_impulse = 0;
  int pass_fiddling = 0;
  int knock_on = 0;
  int command_target = 0;
  int forced_target = 0;
  int incoming_retain_override = 0;
  int max_power_profile_present = 0;
  int difficulty_profile_present = 0;
  int native_ball_direction_present = 0;
  int contact_bodypart_present = 0;
  int incoming_retain_present = 0;
  int outgoing_retain_present = 0;
  int contact_position_offset_nonzero = 0;
  int animation_positions_present = 0;
  std::vector<int> observed_elapsed_ms;
  std::vector<float> desired_ball_heights;
  std::vector<int> contact_frames;
  std::vector<int> delays_ms;
  std::vector<float> full_ball_distances;
  std::vector<float> bumpy_ride_biases;
  std::vector<float> impulse_request_speeds;
};
bool &ContactAuthorityAuditEnabled();
bool IsTrackedScheduledContact(e_FunctionType type);
ContactAuthorityAudit &ContactAuthorityFor(e_FunctionType type);

// ANIM-2 A11.0: body-forecast dual-run. The legacy forecast stays the
// production one; the Player's read-only PreviewRawPose is compared against it
// so the authority can be flipped once the two agree bit-exactly.
int &BakedBodyPreviewChecks();

#endif  // FOOTBALL_SIM_PLAYER_PLAYER_DIAGNOSTICS_HPP
