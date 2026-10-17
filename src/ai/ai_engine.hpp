#ifndef FOOTBALL_AI_AI_ENGINE_HPP
#define FOOTBALL_AI_AI_ENGINE_HPP

#include <array>
#include <memory>

#include "football/ai/ai_config.hpp"
#include "football/ai/player_policy.hpp"
#include "football/ai/tactical_board.hpp"
#include "football/observation/world_state.hpp"
#include "football/pitch/pitch.hpp"
#include "football/player/player_control_set.hpp"
#include "football/runtime/match_config.hpp"

namespace football::ai {

// The AI composition root. It owns the persistent tactical boards (the coach's
// intent) and the one active IPlayerPolicy, and never owns actors, match state
// or RNG. GameEnv drives the whole AI layer through Update().
class AIEngine {
 public:
  AIEngine(const football::sim::TeamSetup& home,
           const football::sim::TeamSetup& away,
           const football::pitch::Pitch& pitch,
           const AIConfig& config);
  ~AIEngine();
  AIEngine(const AIEngine&) = delete;
  AIEngine& operator=(const AIEngine&) = delete;

  // Fully replaces this tick's control output via the active policy. The boards
  // are passed read-only; the engine never mutates them here.
  void Update(const WorldState& world, PlayerControlSet& output);

  // Coach-facing tactical intent. A future Coach AI edits these values; the
  // Player policy only reads the copy the engine hands it.
  TacticalBoard& tactics(football::sim::TeamSide side);
  const TacticalBoard& tactics(football::sim::TeamSide side) const;

 private:
  std::array<TacticalBoard, 2> boards_;
  std::unique_ptr<IPlayerPolicy> policy_;
};

}  // namespace football::ai

#endif  // FOOTBALL_AI_AI_ENGINE_HPP
