// AGENT-1: strict local decoding of model tool calls into typed coach requests.
// The model's arguments are untrusted, so every field is validated before any
// CoachCommand exists; the controlling side is injected, never supplied.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

#include <nlohmann/json.hpp>

#include "ai/coach/coach_tool_decoder.hpp"

namespace {

using nlohmann::json;

using football::ai::BuildUpStyle;
using football::ai::CoachToolDecodeError;
using football::ai::CoachToolDecodeResult;
using football::ai::CoachToolDecoder;
using football::ai::DefendingRole;
using football::ai::GetMatchStateRequest;
using football::ai::GetPlayerInfoRequest;
using football::ai::GetTacticalBoardRequest;
using football::ai::IndividualKey;
using football::ai::InstructionKey;
using football::ai::IssueShout;
using football::ai::MarkingStyle;
using football::ai::Mentality;
using football::ai::PossessionRole;
using football::ai::SetIndividualInstruction;
using football::ai::SetSlotAnchor;
using football::ai::SetSlotRole;
using football::ai::SetTeamInstruction;
using football::ai::SetTransitionInstruction;
using football::ai::ShoutType;
using football::ai::TacticalPhase;
using football::ai::TransitionKey;
using football::ai::TransitionOnLoss;
using football::ai::TransitionOnWin;
using football::sim::TeamSide;

template <typename T>
const T* As(const CoachToolDecodeResult& result) {
  if (!result.request) return nullptr;
  return std::get_if<T>(&*result.request);
}

CoachToolDecodeResult Decode(std::string_view name, const json& arguments,
                             TeamSide self = TeamSide::Home) {
  return CoachToolDecoder::Decode(name, arguments, self);
}

}  // namespace

TEST_CASE("the read tools decode with no arguments", "[ai][coach][decoder]") {
  const auto state = Decode("get_match_state", json::object());
  REQUIRE(state.ok);
  CHECK(state.error == CoachToolDecodeError::Ok);
  CHECK(As<GetMatchStateRequest>(state) != nullptr);

  const auto board = Decode("get_tactical_board", json::object());
  REQUIRE(board.ok);
  CHECK(As<GetTacticalBoardRequest>(board) != nullptr);

  const auto player = Decode("get_player_info", {{"player", 7}});
  REQUIRE(player.ok);
  const auto* info = As<GetPlayerInfoRequest>(player);
  REQUIRE(info != nullptr);
  CHECK(info->player == 7);
}

TEST_CASE("a team instruction decodes per-key value types",
          "[ai][coach][decoder]") {
  const auto scalar = Decode("set_team_instruction", {{"key", "tempo"}, {"value", 0.7}});
  REQUIRE(scalar.ok);
  const auto* number = As<SetTeamInstruction>(scalar);
  REQUIRE(number != nullptr);
  CHECK(number->side == TeamSide::Home);
  CHECK(number->key == InstructionKey::Tempo);
  CHECK(std::get<float>(number->value) == Catch::Approx(0.7f));

  const auto enum_value =
      Decode("set_team_instruction", {{"key", "mentality"}, {"value", "attacking"}});
  REQUIRE(enum_value.ok);
  const auto* mentality = As<SetTeamInstruction>(enum_value);
  REQUIRE(mentality != nullptr);
  CHECK(mentality->key == InstructionKey::Mentality);
  CHECK(std::get<Mentality>(mentality->value) == Mentality::Attacking);

  const auto style =
      Decode("set_team_instruction", {{"key", "build_up"}, {"value", "direct"}});
  REQUIRE(style.ok);
  CHECK(std::get<BuildUpStyle>(As<SetTeamInstruction>(style)->value) ==
        BuildUpStyle::Direct);

  const auto marking =
      Decode("set_team_instruction", {{"key", "marking"}, {"value", "man"}});
  REQUIRE(marking.ok);
  CHECK(std::get<MarkingStyle>(As<SetTeamInstruction>(marking)->value) ==
        MarkingStyle::Man);

  const auto boolean =
      Decode("set_team_instruction", {{"key", "pass_into_space"}, {"value", true}});
  REQUIRE(boolean.ok);
  CHECK(std::get<bool>(As<SetTeamInstruction>(boolean)->value) == true);
}

TEST_CASE("slot, individual and transition tools decode typed values",
          "[ai][coach][decoder]") {
  const auto role = Decode(
      "set_slot_role",
      {{"slot", 3}, {"phase", "in_possession"}, {"role", "playmaker"}});
  REQUIRE(role.ok);
  const auto* role_command = As<SetSlotRole>(role);
  REQUIRE(role_command != nullptr);
  CHECK(role_command->slot == 3);
  CHECK(role_command->phase == TacticalPhase::InPossession);
  CHECK(std::get<PossessionRole>(role_command->role) == PossessionRole::Playmaker);

  const auto defending = Decode(
      "set_slot_role",
      {{"slot", 3}, {"phase", "out_of_possession"}, {"role", "pressing_forward"}});
  REQUIRE(defending.ok);
  CHECK(std::get<DefendingRole>(As<SetSlotRole>(defending)->role) ==
        DefendingRole::PressingForward);

  const auto anchor = Decode(
      "set_slot_anchor",
      {{"slot", 5}, {"phase", "out_of_possession"}, {"u", 0.3}, {"v", -0.5}});
  REQUIRE(anchor.ok);
  const auto* anchor_command = As<SetSlotAnchor>(anchor);
  REQUIRE(anchor_command != nullptr);
  CHECK(anchor_command->anchor.u == Catch::Approx(0.3f));
  CHECK(anchor_command->anchor.v == Catch::Approx(-0.5f));

  const auto scalar = Decode(
      "set_individual_instruction",
      {{"slot", 9}, {"key", "pressing"}, {"value", 0.9}});
  REQUIRE(scalar.ok);
  const auto* individual = As<SetIndividualInstruction>(scalar);
  REQUIRE(individual != nullptr);
  CHECK(individual->key == IndividualKey::Pressing);
  CHECK(std::get<float>(individual->value) == Catch::Approx(0.9f));

  const auto target = Decode(
      "set_individual_instruction",
      {{"slot", 4}, {"key", "marking_target"}, {"value", 42}});
  REQUIRE(target.ok);
  CHECK(std::get<football::player::PlayerId>(
            As<SetIndividualInstruction>(target)->value) == 42);

  const auto on_win = Decode(
      "set_transition_instruction", {{"key", "on_win"}, {"value", "counter"}});
  REQUIRE(on_win.ok);
  const auto* win = As<SetTransitionInstruction>(on_win);
  REQUIRE(win != nullptr);
  CHECK(win->key == TransitionKey::OnWin);
  CHECK(std::get<TransitionOnWin>(win->value) == TransitionOnWin::Counter);

  const auto on_loss = Decode(
      "set_transition_instruction", {{"key", "on_loss"}, {"value", "counter_press"}});
  REQUIRE(on_loss.ok);
  CHECK(std::get<TransitionOnLoss>(As<SetTransitionInstruction>(on_loss)->value) ==
        TransitionOnLoss::CounterPress);
}

TEST_CASE("a shout decodes its bounded target list", "[ai][coach][decoder]") {
  const auto shout = Decode("shout_to_players", {{"type", "fire_up"}, {"targets", {3, 4}}});
  REQUIRE(shout.ok);
  const auto* command = As<IssueShout>(shout);
  REQUIRE(command != nullptr);
  CHECK(command->side == TeamSide::Home);
  CHECK(command->type == ShoutType::FireUp);
  REQUIRE(command->targets.size() == 2);
  CHECK(command->targets[0] == 3);
  CHECK(command->targets[1] == 4);
}

TEST_CASE("the session side is injected and never taken from the model",
          "[ai][coach][decoder]") {
  const auto result = Decode("set_team_instruction",
                             {{"key", "tempo"}, {"value", 0.6}, {"side", "home"}},
                             TeamSide::Away);
  // The model may not set the side at all: the extra field is rejected.
  CHECK_FALSE(result.ok);
  CHECK(result.error == CoachToolDecodeError::UnknownField);
  CHECK_FALSE(result.request.has_value());

  const auto injected =
      Decode("set_team_instruction", {{"key", "tempo"}, {"value", 0.6}}, TeamSide::Away);
  REQUIRE(injected.ok);
  CHECK(As<SetTeamInstruction>(injected)->side == TeamSide::Away);
}

TEST_CASE("the decoder rejects unknown tools and malformed arguments",
          "[ai][coach][decoder]") {
  const auto unknown = Decode("teleport_ball", json::object());
  CHECK_FALSE(unknown.ok);
  CHECK(unknown.error == CoachToolDecodeError::UnknownTool);

  const auto array = Decode("set_team_instruction", json::array({1, 2}));
  CHECK_FALSE(array.ok);
  CHECK(array.error == CoachToolDecodeError::InvalidArguments);

  const auto malformed = CoachToolDecoder::DecodeJson(
      "set_team_instruction", "{not json", TeamSide::Home);
  CHECK_FALSE(malformed.ok);
  CHECK(malformed.error == CoachToolDecodeError::InvalidArguments);

  const auto parsed = CoachToolDecoder::DecodeJson(
      "set_team_instruction", R"({"key":"tempo","value":0.4})", TeamSide::Home);
  REQUIRE(parsed.ok);
  CHECK(std::get<float>(As<SetTeamInstruction>(parsed)->value) == Catch::Approx(0.4f));
}

TEST_CASE("missing and unknown fields are rejected", "[ai][coach][decoder]") {
  const auto missing = Decode("set_team_instruction", {{"key", "tempo"}});
  CHECK_FALSE(missing.ok);
  CHECK(missing.error == CoachToolDecodeError::MissingField);

  const auto unknown =
      Decode("set_team_instruction", {{"key", "tempo"}, {"value", 0.5}, {"speed", 3}});
  CHECK_FALSE(unknown.ok);
  CHECK(unknown.error == CoachToolDecodeError::UnknownField);

  const auto extra_read = Decode("get_match_state", {{"detail", true}});
  CHECK_FALSE(extra_read.ok);
  CHECK(extra_read.error == CoachToolDecodeError::UnknownField);
}

TEST_CASE("a value whose JSON type does not match its key is rejected",
          "[ai][coach][decoder]") {
  const auto string_scalar =
      Decode("set_team_instruction", {{"key", "tempo"}, {"value", "fast"}});
  CHECK_FALSE(string_scalar.ok);
  CHECK(string_scalar.error == CoachToolDecodeError::WrongType);

  const auto number_enum =
      Decode("set_team_instruction", {{"key", "mentality"}, {"value", 0.5}});
  CHECK_FALSE(number_enum.ok);
  CHECK(number_enum.error == CoachToolDecodeError::WrongType);

  const auto number_bool =
      Decode("set_team_instruction", {{"key", "pass_into_space"}, {"value", 1}});
  CHECK_FALSE(number_bool.ok);
  CHECK(number_bool.error == CoachToolDecodeError::WrongType);

  const auto string_target = Decode(
      "set_individual_instruction",
      {{"slot", 2}, {"key", "marking_target"}, {"value", "9"}});
  CHECK_FALSE(string_target.ok);
  CHECK(string_target.error == CoachToolDecodeError::ValueOutOfRange);

  const auto float_slot = Decode(
      "set_slot_anchor", {{"slot", 1.5}, {"phase", "in_possession"}, {"u", 0.5}, {"v", 0.0}});
  CHECK_FALSE(float_slot.ok);
  CHECK(float_slot.error == CoachToolDecodeError::WrongType);
}

TEST_CASE("out-of-range values are rejected", "[ai][coach][decoder]") {
  for (const double value : {1.5, -0.1}) {
    const auto result =
        Decode("set_team_instruction", {{"key", "pressing_intensity"}, {"value", value}});
    CHECK_FALSE(result.ok);
    CHECK(result.error == CoachToolDecodeError::ValueOutOfRange);
  }

  const auto slot = Decode(
      "set_slot_anchor", {{"slot", 11}, {"phase", "in_possession"}, {"u", 0.5}, {"v", 0.0}});
  CHECK_FALSE(slot.ok);
  CHECK(slot.error == CoachToolDecodeError::ValueOutOfRange);

  const auto u = Decode(
      "set_slot_anchor", {{"slot", 1}, {"phase", "in_possession"}, {"u", 1.2}, {"v", 0.0}});
  CHECK_FALSE(u.ok);
  CHECK(u.error == CoachToolDecodeError::ValueOutOfRange);

  const auto v = Decode(
      "set_slot_anchor", {{"slot", 1}, {"phase", "in_possession"}, {"u", 0.5}, {"v", -1.5}});
  CHECK_FALSE(v.ok);
  CHECK(v.error == CoachToolDecodeError::ValueOutOfRange);

  // A hugely out-of-float-range number is not representable and is rejected.
  const auto huge =
      Decode("set_team_instruction", {{"key", "tempo"}, {"value", 1e300}});
  CHECK_FALSE(huge.ok);
  CHECK(huge.error == CoachToolDecodeError::NonFiniteValue);
}

TEST_CASE("invalid enum members are rejected", "[ai][coach][decoder]") {
  const auto key = Decode("set_team_instruction", {{"key", "nope"}, {"value", 0.5}});
  CHECK_FALSE(key.ok);
  CHECK(key.error == CoachToolDecodeError::InvalidEnumValue);

  const auto mentality =
      Decode("set_team_instruction", {{"key", "mentality"}, {"value", "angry"}});
  CHECK_FALSE(mentality.ok);
  CHECK(mentality.error == CoachToolDecodeError::InvalidEnumValue);

  const auto phase = Decode(
      "set_slot_anchor", {{"slot", 1}, {"phase", "both"}, {"u", 0.5}, {"v", 0.0}});
  CHECK_FALSE(phase.ok);
  CHECK(phase.error == CoachToolDecodeError::InvalidEnumValue);

  // A defending-only role is illegal while the phase is in possession.
  const auto mismatched = Decode(
      "set_slot_role",
      {{"slot", 1}, {"phase", "in_possession"}, {"role", "pressing_forward"}});
  CHECK_FALSE(mismatched.ok);
  CHECK(mismatched.error == CoachToolDecodeError::InvalidEnumValue);

  const auto shout_type = Decode("shout_to_players", {{"type", "scream"}, {"targets", {1}}});
  CHECK_FALSE(shout_type.ok);
  CHECK(shout_type.error == CoachToolDecodeError::InvalidEnumValue);
}

TEST_CASE("player ids and shout target lists are bounded",
          "[ai][coach][decoder]") {
  for (const std::int64_t player : {0LL, -1LL, 4294967295LL}) {
    const auto result = Decode("get_player_info", {{"player", player}});
    CHECK_FALSE(result.ok);
    CHECK(result.error == CoachToolDecodeError::ValueOutOfRange);
  }

  const auto empty = Decode("shout_to_players", {{"type", "focus"}, {"targets", json::array()}});
  CHECK_FALSE(empty.ok);
  CHECK(empty.error == CoachToolDecodeError::EmptyTargets);

  json too_many = json::array();
  for (int i = 0; i < 12; ++i) too_many.push_back(i + 1);
  const auto many = Decode("shout_to_players", {{"type", "focus"}, {"targets", too_many}});
  CHECK_FALSE(many.ok);
  CHECK(many.error == CoachToolDecodeError::TooManyTargets);

  const auto bad_target =
      Decode("shout_to_players", {{"type", "focus"}, {"targets", json::array({1, 0})}});
  CHECK_FALSE(bad_target.ok);
  CHECK(bad_target.error == CoachToolDecodeError::ValueOutOfRange);

  const auto not_array = Decode("shout_to_players", {{"type", "focus"}, {"targets", 3}});
  CHECK_FALSE(not_array.ok);
  CHECK(not_array.error == CoachToolDecodeError::WrongType);
}

TEST_CASE("a failed decode never leaks a request", "[ai][coach][decoder]") {
  const auto result =
      Decode("set_team_instruction", {{"key", "tempo"}, {"value", 9.0}});
  REQUIRE_FALSE(result.ok);
  CHECK_FALSE(result.request.has_value());
}
