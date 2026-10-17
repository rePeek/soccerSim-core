#ifndef FOOTBALL_AI_SHOUT_HPP
#define FOOTBALL_AI_SHOUT_HPP

#include <cstdint>

#include "football/ai/coach_command.hpp"
#include "football/player/player_definition.hpp"
#include "football/runtime/match_config.hpp"

namespace football::ai {

// A shout is short-lived psychological communication, not a tactical edit. It
// lives outside TacticalBoard, decays on a fixed tick schedule, and never
// changes a player's base abilities, animation or physical state.
struct ActiveShout {
  football::sim::TeamSide side = football::sim::TeamSide::Home;
  football::player::PlayerId target = football::player::kInvalidPlayerId;
  ShoutType type = ShoutType::Encourage;
  std::uint64_t issued_tick = 0;
  std::uint64_t expires_at_tick = 0;
  // Deterministic response magnitude in [0,1]; v1 has no personality database,
  // so every player responds the same. A future observation layer may vary it.
  float response_strength = 1.0f;
};

// Bounded mental offsets. They are transient inputs to a future policy context,
// never stored on the player entity and never applied to base skills.
struct PlayerMentalEffect {
  float motivation = 0.0f;
  float confidence = 0.0f;
  float focus = 0.0f;
  float composure = 0.0f;
};

}  // namespace football::ai

#endif  // FOOTBALL_AI_SHOUT_HPP
