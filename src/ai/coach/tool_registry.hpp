#ifndef FOOTBALL_AI_COACH_TOOL_REGISTRY_HPP
#define FOOTBALL_AI_COACH_TOOL_REGISTRY_HPP

#include <optional>
#include <span>
#include <string_view>

#include "football/ai/coach_command.hpp"
#include "football/observation/world_state.hpp"

namespace football::ai {

// Tool descriptions for the future provider boundary. v1 is a static list plus
// typed read helpers; it does not implement a general JSON-schema engine.
struct CoachToolDescriptor {
  std::string_view name;
  bool mutating;
  std::string_view summary;
};

// Read-only projections. They expose only the interpreting coach's own side;
// opponent private tactics are never readable.
struct CoachMatchStateSummary {
  football::sim::TeamSide self = football::sim::TeamSide::Home;
  std::uint64_t tick = 0;
  MatchPhase phase = MatchPhase::PreMatch;
  int self_score = 0;
  int opponent_score = 0;
  bool has_possession = false;
  bool ball_in_play = false;
  bool restart_pending = false;
};

struct CoachPlayerInfo {
  football::player::PlayerId id = football::player::kInvalidPlayerId;
  football::sim::TeamSide side = football::sim::TeamSide::Home;
  football::math::Vector3 position = football::math::Vector3(0);
  bool active = false;
  bool has_possession = false;
  e_PlayerRole role = e_PlayerRole_GK;
};

class CoachToolRegistry {
 public:
  static std::span<const CoachToolDescriptor> Tools();
  static bool IsKnown(std::string_view name);
  static bool IsMutating(std::string_view name);

  static CoachMatchStateSummary MatchState(const WorldState& world,
                                           football::sim::TeamSide self);
  // Own-side plan only. Returns nullopt for a legacy board.
  static std::optional<TacticalPlanV2> TacticalPlan(const TacticalBoard& board);
  // Own-side player only. Returns nullopt for an unknown or opponent player.
  static std::optional<CoachPlayerInfo> PlayerInfo(
      const WorldState& world, football::sim::TeamSide self,
      football::player::PlayerId player);
};

}  // namespace football::ai

#endif  // FOOTBALL_AI_COACH_TOOL_REGISTRY_HPP
