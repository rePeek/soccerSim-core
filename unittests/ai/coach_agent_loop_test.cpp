// AGENT-2/AGENT-3A: the provider-agnostic agent loop, the session history and
// the staged tool executor. A scripted football::model MockModelProvider drives
// real multi-turn tool calling with no network; every write must come out
// staged, never applied.
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "ai/coach/coach_agent_loop.hpp"
#include "ai/coach/coach_session.hpp"
#include "ai/coach/coach_tool_executor.hpp"
#include "ai_test_world.hpp"
#include "football/ai/tactical_board.hpp"
#include "football/model/model_protocol.hpp"
#include "model/mock/mock_model_provider.hpp"

namespace {

using football::ai::CoachAgentConfig;
using football::ai::CoachAgentLoop;
using football::ai::CoachSession;
using football::ai::CoachToolExecution;
using football::ai::CoachToolExecutor;
using football::ai::CoachToolView;
using football::ai::CoachTurnResult;
using football::ai::CoachTurnSnapshot;
using football::ai::GetTacticalBoardRequest;
using football::ai::InstructionKey;
using football::ai::SetTeamInstruction;
using football::ai::StagedCoachCommand;
using football::ai::TacticalBoard;
using football::ai::test::MakeTeam;
using football::ai::test::MakeWorld;
using football::model::MockModelProvider;
using football::model::ModelMessage;
using football::model::ModelRole;
using football::model::ModelToolCall;
using football::sim::TeamSide;

ModelToolCall Call(std::string id, std::string name, std::string arguments) {
  ModelToolCall call;
  call.id = std::move(id);
  call.name = std::move(name);
  call.arguments_json = std::move(arguments);
  return call;
}

// A synthetic observed world plus the Home team's own tactical board. The
// `snapshot.view` point back into this fixture, so it is never copied.
struct Fixture {
  football::sim::TeamSetup home = MakeTeam(0);
  football::sim::TeamSetup away = MakeTeam(100);
  WorldState world = MakeWorld(home, away);
  TacticalBoard board =
      football::ai::MakeTacticalBoard(home, TeamSide::Home, football::pitch::Pitch{});
  CoachTurnSnapshot snapshot;

  explicit Fixture(TeamSide self = TeamSide::Home) {
    board = football::ai::MakeTacticalBoard(
        self == TeamSide::Home ? home : away, self, football::pitch::Pitch{});
    snapshot.context.self = self;
    snapshot.context.observed_tick = world.tick;
    snapshot.context.board_revision = board.revision;
    snapshot.context.phase = world.phase;
    snapshot.view.self = self;
    snapshot.view.world = &world;
    snapshot.view.board = &board;
  }
};

const SetTeamInstruction* AsTeamInstruction(const StagedCoachCommand& staged) {
  return std::get_if<SetTeamInstruction>(&staged.command);
}

bool Contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

}  // namespace

TEST_CASE("a read tool result is fed back and the turn finalizes",
          "[ai][coach][agent][loop]") {
  Fixture fixture;
  MockModelProvider provider;
  provider.PushToolCalls({Call("c1", "get_match_state", "{}")});
  provider.PushText("Nothing to change.");
  CoachSession session(TeamSide::Home);
  CoachAgentLoop loop(provider);

  const CoachTurnResult result = loop.Run(session, fixture.snapshot);
  REQUIRE(result.ok);
  CHECK_FALSE(result.truncated);
  CHECK(result.final_text == "Nothing to change.");
  CHECK(result.model_turns == 2);
  CHECK(result.tool_calls == 1);
  CHECK(result.staged.empty());

  REQUIRE(result.transcript.size() == 3);
  CHECK(result.transcript[0].role == ModelRole::Assistant);
  CHECK(result.transcript[1].role == ModelRole::Tool);
  CHECK(result.transcript[1].tool_call_id == "c1");
  CHECK(Contains(result.transcript[1].content, "has_possession"));
  CHECK(result.transcript[2].role == ModelRole::Assistant);

  // The model's second request carried the tool result, matched by call id,
  // and every request advertises this team's nine tools.
  REQUIRE(provider.requests().size() == 2);
  CHECK(provider.requests()[1].tools.size() == 9);
  CHECK(provider.requests()[1].tools[0].name == "get_match_state");
  const std::vector<ModelMessage>& second = provider.requests()[1].messages;
  REQUIRE(second.size() >= 4);
  CHECK(second[second.size() - 1].role == ModelRole::Tool);
  CHECK(second[second.size() - 1].tool_call_id == "c1");
}

TEST_CASE("multiple sequential tool calls each get a result",
          "[ai][coach][agent][loop]") {
  Fixture fixture;
  MockModelProvider provider;
  provider.PushToolCalls({Call("c1", "get_player_info", R"({"player":1})")});
  provider.PushToolCalls({Call("c2", "get_tactical_board", "{}")});
  provider.PushText("Reviewed.");
  CoachSession session(TeamSide::Home);
  CoachAgentLoop loop(provider);

  const CoachTurnResult result = loop.Run(session, fixture.snapshot);
  REQUIRE(result.ok);
  CHECK(result.model_turns == 3);
  CHECK(result.tool_calls == 2);
  REQUIRE(result.transcript.size() == 5);
  CHECK(result.transcript[1].tool_call_id == "c1");
  CHECK(Contains(result.transcript[1].content, "\"active\""));
  CHECK(result.transcript[3].tool_call_id == "c2");
  CHECK(Contains(result.transcript[3].content, "in_possession"));

  REQUIRE(provider.requests().size() == 3);
  const std::vector<ModelMessage>& third = provider.requests()[2].messages;
  CHECK(third[third.size() - 1].tool_call_id == "c2");
}

TEST_CASE("a write tool stages a command and never applies it",
          "[ai][coach][agent][loop]") {
  Fixture fixture;
  const std::uint64_t revision = fixture.board.revision;
  MockModelProvider provider;
  provider.PushToolCalls(
      {Call("w1", "set_team_instruction", R"({"key":"tempo","value":0.8})")});
  provider.PushText("Tempo raised.");
  CoachSession session(TeamSide::Home);
  CoachAgentLoop loop(provider);

  const CoachTurnResult result = loop.Run(session, fixture.snapshot);
  REQUIRE(result.ok);
  REQUIRE(result.staged.size() == 1);
  CHECK(result.staged[0].tool_call_id == "w1");
  const SetTeamInstruction* command = AsTeamInstruction(result.staged[0]);
  REQUIRE(command != nullptr);
  CHECK(command->side == TeamSide::Home);
  CHECK(command->key == InstructionKey::Tempo);
  CHECK(std::get<float>(command->value) == 0.8f);

  // Staging is not application: the board is untouched and the payload says so.
  CHECK(fixture.board.revision == revision);
  CHECK(Contains(result.transcript[1].content, "\"staged\""));
}

TEST_CASE("the controlling side is injected for the whole loop",
          "[ai][coach][agent][loop]") {
  Fixture fixture(TeamSide::Away);
  MockModelProvider provider;
  provider.PushToolCalls(
      {Call("w1", "set_team_instruction", R"({"key":"tempo","value":0.6})")});
  provider.PushText("Done.");
  CoachSession session(TeamSide::Away);
  CoachAgentLoop loop(provider);

  const CoachTurnResult result = loop.Run(session, fixture.snapshot);
  REQUIRE(result.ok);
  REQUIRE(result.staged.size() == 1);
  CHECK(AsTeamInstruction(result.staged[0])->side == TeamSide::Away);

  // The model cannot set the side itself; the extra field is rejected.
  MockModelProvider spoof;
  spoof.PushToolCalls({Call(
      "w1", "set_team_instruction", R"({"key":"tempo","value":0.6,"side":"home"})")});
  spoof.PushText("Done.");
  CoachSession spoof_session(TeamSide::Away);
  CoachAgentLoop spoof_loop(spoof);
  const CoachTurnResult spoofed = spoof_loop.Run(spoof_session, fixture.snapshot);
  REQUIRE(spoofed.ok);
  CHECK(spoofed.staged.empty());
  CHECK(Contains(spoofed.transcript[1].content, "unknown_field"));
}

TEST_CASE("invalid arguments are rejected as a tool result, not staged",
          "[ai][coach][agent][loop]") {
  Fixture fixture;
  MockModelProvider provider;
  provider.PushToolCalls(
      {Call("w1", "set_team_instruction", R"({"key":"tempo","value":"fast"})")});
  provider.PushText("Retry later.");
  CoachSession session(TeamSide::Home);
  CoachAgentLoop loop(provider);

  const CoachTurnResult result = loop.Run(session, fixture.snapshot);
  REQUIRE(result.ok);
  CHECK(result.staged.empty());
  CHECK(Contains(result.transcript[1].content, "rejected"));
  CHECK(Contains(result.transcript[1].content, "wrong_type"));
}

TEST_CASE("an unknown write target is rejected by the read path",
          "[ai][coach][agent][loop]") {
  Fixture fixture;
  MockModelProvider provider;
  // Player 100 is on the away side, so the home coach may not read it.
  provider.PushToolCalls({Call("p1", "get_player_info", R"({"player":100})")});
  provider.PushText("No such player.");
  CoachSession session(TeamSide::Home);
  CoachAgentLoop loop(provider);

  const CoachTurnResult result = loop.Run(session, fixture.snapshot);
  REQUIRE(result.ok);
  CHECK(result.staged.empty());
  CHECK(Contains(result.transcript[1].content, "rejected"));
  CHECK(Contains(result.transcript[1].content, "unknown_player"));
}

TEST_CASE("an unknown tool is rejected and fed back",
          "[ai][coach][agent][loop]") {
  Fixture fixture;
  MockModelProvider provider;
  provider.PushToolCalls({Call("x1", "teleport_ball", R"({"x":0})")});
  provider.PushText("Sorry.");
  CoachSession session(TeamSide::Home);
  CoachAgentLoop loop(provider);

  const CoachTurnResult result = loop.Run(session, fixture.snapshot);
  REQUIRE(result.ok);
  CHECK(result.staged.empty());
  CHECK(Contains(result.transcript[1].content, "unknown_tool"));
}

TEST_CASE("the tool-call budget truncates the turn and discards its proposal",
          "[ai][coach][agent][loop]") {
  Fixture fixture;
  MockModelProvider provider;
  provider.PushToolCalls(
      {Call("a", "set_team_instruction", R"({"key":"tempo","value":0.8})"),
       Call("b", "get_match_state", "{}")});
  provider.PushText("Never reached.");
  CoachSession session(TeamSide::Home);
  CoachAgentConfig config;
  config.max_tool_calls = 1;
  CoachAgentLoop loop(provider, config);

  const CoachTurnResult result = loop.Run(session, fixture.snapshot);
  CHECK_FALSE(result.ok);
  CHECK(result.truncated);
  CHECK(result.error == "tool_budget_exhausted");
  CHECK(result.tool_calls == 1);
  CHECK(result.staged.empty());
  // Every call is still answered, so the transcript stays a valid pairing.
  REQUIRE(result.transcript.size() == 3);
  CHECK(Contains(result.transcript[2].content, "tool_budget_exhausted"));
}

TEST_CASE("the model-turn budget truncates a turn that never finalizes",
          "[ai][coach][agent][loop]") {
  Fixture fixture;
  MockModelProvider provider;
  provider.PushToolCalls({Call("a", "get_match_state", "{}")});
  provider.PushToolCalls({Call("b", "get_match_state", "{}")});
  provider.PushText("Never reached.");
  CoachSession session(TeamSide::Home);
  CoachAgentConfig config;
  config.max_model_turns = 2;
  CoachAgentLoop loop(provider, config);

  const CoachTurnResult result = loop.Run(session, fixture.snapshot);
  CHECK_FALSE(result.ok);
  CHECK(result.truncated);
  CHECK(result.error == "model_turn_budget_exhausted");
  CHECK(result.model_turns == 2);
  CHECK(result.tool_calls == 2);
  CHECK(result.staged.empty());
}

TEST_CASE("a provider error aborts the turn without staging",
          "[ai][coach][agent][loop]") {
  Fixture fixture;
  MockModelProvider provider;
  provider.PushToolCalls(
      {Call("a", "set_team_instruction", R"({"key":"tempo","value":0.8})")});
  provider.PushError("network down");
  CoachSession session(TeamSide::Home);
  CoachAgentLoop loop(provider);

  const CoachTurnResult result = loop.Run(session, fixture.snapshot);
  CHECK_FALSE(result.ok);
  CHECK_FALSE(result.truncated);
  CHECK(result.error == "network down");
  CHECK(result.model_turns == 2);
  CHECK(result.staged.empty());
}

TEST_CASE("the session keeps whole turns and drops the oldest",
          "[ai][coach][agent][loop]") {
  Fixture fixture;
  MockModelProvider provider;
  provider.PushText("first");
  provider.PushText("second");
  CoachSession session(TeamSide::Home, /*max_turns=*/1);
  CoachAgentLoop loop(provider);

  fixture.snapshot.context.observed_tick = 100;
  REQUIRE(loop.Run(session, fixture.snapshot).ok);
  CHECK(session.TurnCount() == 1);
  const std::size_t after_first = session.Messages().size();
  CHECK(after_first == 3);  // system + user + assistant

  fixture.snapshot.context.observed_tick = 200;
  REQUIRE(loop.Run(session, fixture.snapshot).ok);
  CHECK(session.TurnCount() == 1);
  CHECK(session.Messages().size() == 3);
  // The first turn's user message is gone; only the newest tick remains.
  CHECK(Contains(session.Messages()[1].content, "200"));
  CHECK_FALSE(Contains(session.Messages()[1].content, "100"));
}

TEST_CASE("the executor reports an unavailable snapshot without crashing",
          "[ai][coach][agent][loop]") {
  const CoachToolExecutor executor;
  const GetTacticalBoardRequest request;
  const CoachToolExecution missing =
      executor.Execute(request, CoachToolView{TeamSide::Home, nullptr, nullptr});
  CHECK_FALSE(missing.ok);
  CHECK(Contains(missing.content_json, "board_unavailable"));
  CHECK_FALSE(missing.command.has_value());
}
