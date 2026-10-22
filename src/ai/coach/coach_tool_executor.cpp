#include "coach_tool_executor.hpp"

#include <utility>
#include <variant>

#include "coach_tool_json.hpp"
#include "tool_registry.hpp"

namespace football::ai {
namespace {

using nlohmann::json;

CoachToolExecution Payload(json payload) {
  CoachToolExecution execution;
  execution.ok = true;
  execution.content_json = payload.dump();
  return execution;
}

CoachToolExecution Rejected(std::string_view error) {
  CoachToolExecution execution;
  execution.ok = false;
  execution.content_json =
      json{{"status", "rejected"}, {"error", std::string(error)}}.dump();
  return execution;
}

// A validated write is staged, not applied. It only becomes a board change when
// the turn's commit boundary accepts the whole batch.
template <typename Command>
CoachToolExecution Staged(Command command) {
  CoachToolExecution execution;
  execution.ok = true;
  execution.content_json = json{{"status", "staged"}}.dump();
  execution.command = CoachCommand{std::move(command)};
  return execution;
}

CoachToolExecution ExecuteTyped(const GetMatchStateRequest&,
                           const CoachToolView& view) {
  if (view.world == nullptr) return Rejected("world_unavailable");
  return Payload(MatchStateJson(CoachToolRegistry::MatchState(*view.world, view.self)));
}

CoachToolExecution ExecuteTyped(const GetTacticalBoardRequest&,
                           const CoachToolView& view) {
  if (view.board == nullptr) return Rejected("board_unavailable");
  return Payload(TacticalPlanJson(CoachToolRegistry::TacticalPlanOf(*view.board)));
}

CoachToolExecution ExecuteTyped(const GetPlayerInfoRequest& request,
                           const CoachToolView& view) {
  if (view.world == nullptr) return Rejected("world_unavailable");
  const std::optional<CoachPlayerInfo> info =
      CoachToolRegistry::PlayerInfo(*view.world, view.self, request.player);
  if (!info) return Rejected("unknown_player");
  return Payload(PlayerInfoJson(*info));
}

CoachToolExecution ExecuteTyped(const SetTeamInstruction& command,
                           const CoachToolView&) {
  return Staged(command);
}
CoachToolExecution ExecuteTyped(const SetSlotRole& command, const CoachToolView&) {
  return Staged(command);
}
CoachToolExecution ExecuteTyped(const SetSlotAnchor& command, const CoachToolView&) {
  return Staged(command);
}
CoachToolExecution ExecuteTyped(const SetIndividualInstruction& command,
                           const CoachToolView&) {
  return Staged(command);
}
CoachToolExecution ExecuteTyped(const SetTransitionInstruction& command,
                           const CoachToolView&) {
  return Staged(command);
}
CoachToolExecution ExecuteTyped(const IssueShout& command, const CoachToolView&) {
  return Staged(command);
}

}  // namespace

CoachToolExecution CoachToolExecutor::Execute(const CoachToolRequest& request,
                                              const CoachToolView& view) {
  return std::visit([&view](const auto& typed) { return ExecuteTyped(typed, view); },
                    request);
}

}  // namespace football::ai
