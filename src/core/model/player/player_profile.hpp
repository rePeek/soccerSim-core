#ifndef _HPP_CORE_MODEL_PLAYER_PROFILE
#define _HPP_CORE_MODEL_PLAYER_PROFILE

namespace football::model {

// Match-lifetime physical properties. Ability is not current condition:
// balance is a skill, not the player's instantaneous stability after contact.
struct PlayerPhysicalProfile {
  float height = 1.80f;       // meters
  float mass = 75.0f;         // kilograms; not inferred from legacy ability
  float bodyRadius = 0.36f;   // ordinary ground-contact radius (meters)
  float strength = 0.5f;      // neutral until a strength database mapping exists
  float balance = 0.5f;       // legacy physical_balance ability
};

struct PlayerProfile {
  PlayerPhysicalProfile physical;
};

}  // namespace football::model

#endif  // _HPP_CORE_MODEL_PLAYER_PROFILE
