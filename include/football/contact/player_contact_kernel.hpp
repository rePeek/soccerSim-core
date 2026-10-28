#ifndef FOOTBALL_PLAYER_CONTACT_KERNEL_HPP
#define FOOTBALL_PLAYER_CONTACT_KERNEL_HPP

#include "football/foundation/math/vector3.hpp"
#include "football/contact/player_body_collider.hpp"
#include "football/contact/player_ground_collider.hpp"
#include "football/player/player_action.hpp"
#include "football/player/player_definition.hpp"

namespace football::sim::player {

// Pure contact inputs captured immediately before one pair is resolved.
struct ContactActorSnapshot {
  football::player::PlayerId id = football::player::kInvalidPlayerId;
  int team_id = 0;

  football::math::Vector3 position;
  PlayerGroundCollider collider;

  football::math::Vector3 velocity;
  football::math::Vector3 facing;
  football::math::Vector3 direction_vec;
  float relative_body_angle = 0.0f;
  float float_velocity = 0.0f;

  PlayerActionState action;

  float technical_standingtackle = 0.0f;
  float technical_slidingtackle = 0.0f;
  float physical_balance = 0.0f;
  football::math::Vector3 decaying_position_offset;

  bool has_possession = false;
  bool has_best_possession = false;
  bool global_designated_possession = false;
  bool team_designated_possession = false;
};

struct ContactPairEnvironment {
  football::math::Vector3 ball_next_position;
};

struct ContactTripCandidate {
  bool active = false;
  football::math::Vector3 direction;
  float severity = 0.0f;
  int trip_type = 0;
};

struct ContactPairResult {
  float distance = 0.0f;

  bool first_has_offset = false;
  bool second_has_offset = false;
  football::math::Vector3 first_offset;
  football::math::Vector3 second_offset;

  bool first_similar = false;
  bool second_similar = false;
  float first_similar_force = 0.0f;
  float second_similar_force = 0.0f;

  // True when the pair is inside the trip-radius gate; the caller may ask the
  // rule layer for a foul assessment with these sensitivities.
  bool trip_range = false;
  float first_sensitivity = 0.0f;
  float second_sensitivity = 0.0f;
  ContactTripCandidate first_trip;
  ContactTripCandidate second_trip;
};

// Pure pair collision kernel. It never reads or writes a Player. The caller
// applies the offsets, accumulates the similar-movement forces and submits the
// trip reactions in the legacy order.
ContactPairResult ResolveContactPair(const ContactActorSnapshot& first,
                                     const ContactActorSnapshot& second,
                                     const ContactPairEnvironment& environment);

}  // namespace football::sim::player

#endif  // FOOTBALL_PLAYER_CONTACT_KERNEL_HPP
