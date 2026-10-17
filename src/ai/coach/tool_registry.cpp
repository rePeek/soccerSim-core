#include "tool_registry.hpp"

namespace football::ai {
namespace {
using football::sim::TeamSide;

constexpr CoachToolDescriptor kTools[] = {
    {"get_match_state", false, "observed tick, phase, score, possession facts"},
    {"get_tactical_board", false, "own persistent V2 plan (read-only)"},
    {"get_player_info", false, "own player's public observation values"},
    {"set_team_instruction", true, "set one team instruction field"},
    {"set_slot_role", true, "set an in/out-of-possession slot role"},
    {"set_slot_anchor", true, "set an in/out-of-possession slot anchor"},
    {"set_individual_instruction", true, "set one slot's individual override"},
    {"set_transition_instruction", true, "set the on-win/on-loss transition"},
    {"shout_to_players", true, "issue a bounded psychological shout"},
};
}  // namespace

std::span<const CoachToolDescriptor> CoachToolRegistry::Tools() {
  return kTools;
}

bool CoachToolRegistry::IsKnown(std::string_view name) {
  for (const CoachToolDescriptor& tool : kTools)
    if (tool.name == name) return true;
  return false;
}

bool CoachToolRegistry::IsMutating(std::string_view name) {
  for (const CoachToolDescriptor& tool : kTools)
    if (tool.name == name) return tool.mutating;
  return false;
}

CoachMatchStateSummary CoachToolRegistry::MatchState(const WorldState& world,
                                                     TeamSide self) {
  const TeamSide opponent =
      self == TeamSide::Home ? TeamSide::Away : TeamSide::Home;
  CoachMatchStateSummary summary;
  summary.self = self;
  summary.tick = world.tick;
  summary.phase = world.phase;
  summary.self_score = world.teams[static_cast<unsigned>(self)].score;
  summary.opponent_score = world.teams[static_cast<unsigned>(opponent)].score;
  summary.has_possession = world.teams[static_cast<unsigned>(self)].has_possession;
  summary.ball_in_play = world.ball_in_play;
  summary.restart_pending = world.restart_pending;
  return summary;
}

std::optional<TacticalPlanV2> CoachToolRegistry::TacticalPlan(
    const TacticalBoard& board) {
  if (const TacticalPlanV2* plan = AdvancedPlan(board)) return *plan;
  return std::nullopt;
}

std::optional<CoachPlayerInfo> CoachToolRegistry::PlayerInfo(
    const WorldState& world, TeamSide self,
    football::player::PlayerId player) {
  for (const WorldPlayerState& observed : world.players) {
    if (observed.id != player) continue;
    if (observed.side != self) return std::nullopt;
    CoachPlayerInfo info;
    info.id = observed.id;
    info.side = observed.side;
    info.position = observed.position;
    info.active = observed.active;
    info.has_possession = observed.has_possession;
    info.role = observed.role;
    return info;
  }
  return std::nullopt;
}

}  // namespace football::ai
