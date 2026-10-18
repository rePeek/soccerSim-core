#include "coach_tool_decoder.hpp"

#include <cmath>
#include <initializer_list>
#include <limits>

#include "coach_tool_names.hpp"

namespace football::ai {
namespace {

using nlohmann::json;
using football::player::PlayerId;
using football::sim::TeamSide;

struct Ctx {
  const json* object = nullptr;
  CoachToolDecodeResult result;

  bool Fail(CoachToolDecodeError code, std::string message) {
    result.ok = false;
    result.error = code;
    result.message = std::move(message);
    result.request.reset();
    return false;
  }
};

const json* Field(Ctx& ctx, const char* key) {
  const auto it = ctx.object->find(key);
  return it == ctx.object->end() ? nullptr : &(*it);
}

bool RequireObject(Ctx& ctx) {
  if (!ctx.object->is_object())
    return ctx.Fail(CoachToolDecodeError::InvalidArguments,
                    "arguments must be a JSON object");
  return true;
}

bool CheckFields(Ctx& ctx, std::initializer_list<std::string_view> allowed) {
  for (auto it = ctx.object->begin(); it != ctx.object->end(); ++it) {
    bool known = false;
    for (std::string_view name : allowed) {
      if (it.key() == name) {
        known = true;
        break;
      }
    }
    if (!known)
      return ctx.Fail(CoachToolDecodeError::UnknownField,
                      "unexpected field: " + it.key());
  }
  return true;
}

bool Number(Ctx& ctx, const char* key, float& out) {
  const json* field = Field(ctx, key);
  if (field == nullptr)
    return ctx.Fail(CoachToolDecodeError::MissingField,
                    std::string("missing field: ") + key);
  if (!field->is_number())
    return ctx.Fail(CoachToolDecodeError::WrongType,
                    std::string(key) + " must be a number");
  const double value = field->get<double>();
  out = static_cast<float>(value);
  if (!std::isfinite(out))
    return ctx.Fail(CoachToolDecodeError::NonFiniteValue,
                    std::string(key) + " must be a finite number");
  return true;
}

bool Integer(Ctx& ctx, const char* key, std::int64_t& out) {
  const json* field = Field(ctx, key);
  if (field == nullptr)
    return ctx.Fail(CoachToolDecodeError::MissingField,
                    std::string("missing field: ") + key);
  if (!field->is_number_integer())
    return ctx.Fail(CoachToolDecodeError::WrongType,
                    std::string(key) + " must be an integer");
  if (field->is_number_unsigned()) {
    const std::uint64_t raw = field->get<std::uint64_t>();
    if (raw > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
      return ctx.Fail(CoachToolDecodeError::ValueOutOfRange,
                      std::string(key) + " is out of integer range");
    out = static_cast<std::int64_t>(raw);
  } else {
    out = field->get<std::int64_t>();
  }
  return true;
}

bool Boolean(Ctx& ctx, const char* key, bool& out) {
  const json* field = Field(ctx, key);
  if (field == nullptr)
    return ctx.Fail(CoachToolDecodeError::MissingField,
                    std::string("missing field: ") + key);
  if (!field->is_boolean())
    return ctx.Fail(CoachToolDecodeError::WrongType,
                    std::string(key) + " must be a boolean");
  out = field->get<bool>();
  return true;
}

bool String(Ctx& ctx, const char* key, std::string& out) {
  const json* field = Field(ctx, key);
  if (field == nullptr)
    return ctx.Fail(CoachToolDecodeError::MissingField,
                    std::string("missing field: ") + key);
  if (!field->is_string())
    return ctx.Fail(CoachToolDecodeError::WrongType,
                    std::string(key) + " must be a string");
  out = field->get<std::string>();
  return true;
}

bool InRange(Ctx& ctx, const char* key, float value, float low, float high) {
  if (!(value >= low && value <= high))
    return ctx.Fail(CoachToolDecodeError::ValueOutOfRange,
                    std::string(key) + " is out of range");
  return true;
}

std::optional<PlayerId> AsPlayerId(const json& value) {
  if (!value.is_number_integer()) return std::nullopt;
  std::int64_t raw = 0;
  if (value.is_number_unsigned()) {
    const std::uint64_t wide = value.get<std::uint64_t>();
    if (wide > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
      return std::nullopt;
    raw = static_cast<std::int64_t>(wide);
  } else {
    raw = value.get<std::int64_t>();
  }
  if (raw <= 0 || static_cast<std::uint64_t>(raw) >= football::player::kInvalidPlayerId)
    return std::nullopt;
  return static_cast<PlayerId>(raw);
}

bool PlayerIdField(Ctx& ctx, const char* key, PlayerId& out) {
  const json* field = Field(ctx, key);
  if (field == nullptr)
    return ctx.Fail(CoachToolDecodeError::MissingField,
                    std::string("missing field: ") + key);
  const std::optional<PlayerId> parsed = AsPlayerId(*field);
  if (!parsed)
    return ctx.Fail(CoachToolDecodeError::ValueOutOfRange,
                    std::string(key) + " is not a valid player id");
  out = *parsed;
  return true;
}

bool SlotField(Ctx& ctx, TacticalSlotId& out) {
  std::int64_t slot = 0;
  if (!Integer(ctx, "slot", slot)) return false;
  if (slot < 0 || slot >= static_cast<std::int64_t>(kTacticalSlotCount))
    return ctx.Fail(CoachToolDecodeError::ValueOutOfRange, "slot is out of range");
  out = static_cast<TacticalSlotId>(slot);
  return true;
}

bool PhaseField(Ctx& ctx, TacticalPhase& out) {
  std::string name;
  if (!String(ctx, "phase", name)) return false;
  const std::optional<TacticalPhase> phase = ParseTacticalPhase(name);
  if (!phase)
    return ctx.Fail(CoachToolDecodeError::InvalidEnumValue, "invalid phase: " + name);
  out = *phase;
  return true;
}

bool DecodeGetMatchState(Ctx& ctx) {
  if (!CheckFields(ctx, {})) return false;
  ctx.result.request = GetMatchStateRequest{};
  return true;
}

bool DecodeGetTacticalBoard(Ctx& ctx) {
  if (!CheckFields(ctx, {})) return false;
  ctx.result.request = GetTacticalBoardRequest{};
  return true;
}

bool DecodeGetPlayerInfo(Ctx& ctx) {
  if (!CheckFields(ctx, {"player"})) return false;
  GetPlayerInfoRequest request;
  if (!PlayerIdField(ctx, "player", request.player)) return false;
  ctx.result.request = request;
  return true;
}

bool DecodeSetTeamInstruction(Ctx& ctx, TeamSide self) {
  if (!CheckFields(ctx, {"key", "value"})) return false;
  std::string key_name;
  if (!String(ctx, "key", key_name)) return false;
  const std::optional<InstructionKey> key = ParseInstructionKey(key_name);
  if (!key)
    return ctx.Fail(CoachToolDecodeError::InvalidEnumValue, "invalid key: " + key_name);

  SetTeamInstruction command;
  command.side = self;
  command.key = *key;
  switch (KindOf(*key)) {
    case InstructionValueKind::Number: {
      float value = 0.0f;
      if (!Number(ctx, "value", value)) return false;
      if (!InRange(ctx, "value", value, 0.0f, 1.0f)) return false;
      command.value = value;
      break;
    }
    case InstructionValueKind::Boolean: {
      bool value = false;
      if (!Boolean(ctx, "value", value)) return false;
      command.value = value;
      break;
    }
    case InstructionValueKind::Mentality: {
      std::string name;
      if (!String(ctx, "value", name)) return false;
      const std::optional<Mentality> value = ParseMentality(name);
      if (!value)
        return ctx.Fail(CoachToolDecodeError::InvalidEnumValue,
                        "invalid mentality: " + name);
      command.value = *value;
      break;
    }
    case InstructionValueKind::BuildUpStyle: {
      std::string name;
      if (!String(ctx, "value", name)) return false;
      const std::optional<BuildUpStyle> value = ParseBuildUpStyle(name);
      if (!value)
        return ctx.Fail(CoachToolDecodeError::InvalidEnumValue,
                        "invalid build-up style: " + name);
      command.value = *value;
      break;
    }
    case InstructionValueKind::MarkingStyle: {
      std::string name;
      if (!String(ctx, "value", name)) return false;
      const std::optional<MarkingStyle> value = ParseMarkingStyle(name);
      if (!value)
        return ctx.Fail(CoachToolDecodeError::InvalidEnumValue,
                        "invalid marking style: " + name);
      command.value = *value;
      break;
    }
  }
  ctx.result.request = command;
  return true;
}

bool DecodeSetSlotRole(Ctx& ctx, TeamSide self) {
  if (!CheckFields(ctx, {"slot", "phase", "role"})) return false;
  SetSlotRole command;
  command.side = self;
  if (!SlotField(ctx, command.slot)) return false;
  if (!PhaseField(ctx, command.phase)) return false;
  std::string role_name;
  if (!String(ctx, "role", role_name)) return false;
  if (command.phase == TacticalPhase::InPossession) {
    const std::optional<PossessionRole> role = ParsePossessionRole(role_name);
    if (!role)
      return ctx.Fail(CoachToolDecodeError::InvalidEnumValue,
                      "invalid in-possession role: " + role_name);
    command.role = *role;
  } else {
    const std::optional<DefendingRole> role = ParseDefendingRole(role_name);
    if (!role)
      return ctx.Fail(CoachToolDecodeError::InvalidEnumValue,
                      "invalid out-of-possession role: " + role_name);
    command.role = *role;
  }
  ctx.result.request = command;
  return true;
}

bool DecodeSetSlotAnchor(Ctx& ctx, TeamSide self) {
  if (!CheckFields(ctx, {"slot", "phase", "u", "v"})) return false;
  SetSlotAnchor command;
  command.side = self;
  if (!SlotField(ctx, command.slot)) return false;
  if (!PhaseField(ctx, command.phase)) return false;
  float u = 0.5f;
  if (!Number(ctx, "u", u)) return false;
  if (!InRange(ctx, "u", u, 0.0f, 1.0f)) return false;
  float v = 0.0f;
  if (!Number(ctx, "v", v)) return false;
  if (!InRange(ctx, "v", v, -1.0f, 1.0f)) return false;
  command.anchor.u = u;
  command.anchor.v = v;
  ctx.result.request = command;
  return true;
}

bool DecodeSetIndividualInstruction(Ctx& ctx, TeamSide self) {
  if (!CheckFields(ctx, {"slot", "key", "value"})) return false;
  SetIndividualInstruction command;
  command.side = self;
  if (!SlotField(ctx, command.slot)) return false;
  std::string key_name;
  if (!String(ctx, "key", key_name)) return false;
  const std::optional<IndividualKey> key = ParseIndividualKey(key_name);
  if (!key)
    return ctx.Fail(CoachToolDecodeError::InvalidEnumValue, "invalid key: " + key_name);
  command.key = *key;
  if (KindOf(*key) == IndividualValueKind::MarkingTarget) {
    PlayerId target = football::player::kInvalidPlayerId;
    if (!PlayerIdField(ctx, "value", target)) return false;
    command.value = target;
  } else {
    float value = 0.0f;
    if (!Number(ctx, "value", value)) return false;
    if (!InRange(ctx, "value", value, 0.0f, 1.0f)) return false;
    command.value = value;
  }
  ctx.result.request = command;
  return true;
}

bool DecodeSetTransitionInstruction(Ctx& ctx, TeamSide self) {
  if (!CheckFields(ctx, {"key", "value"})) return false;
  std::string key_name;
  if (!String(ctx, "key", key_name)) return false;
  const std::optional<TransitionKey> key = ParseTransitionKey(key_name);
  if (!key)
    return ctx.Fail(CoachToolDecodeError::InvalidEnumValue, "invalid key: " + key_name);
  std::string value_name;
  if (!String(ctx, "value", value_name)) return false;

  SetTransitionInstruction command;
  command.side = self;
  command.key = *key;
  if (*key == TransitionKey::OnWin) {
    const std::optional<TransitionOnWin> value = ParseTransitionOnWin(value_name);
    if (!value)
      return ctx.Fail(CoachToolDecodeError::InvalidEnumValue,
                      "invalid on-win mode: " + value_name);
    command.value = *value;
  } else {
    const std::optional<TransitionOnLoss> value = ParseTransitionOnLoss(value_name);
    if (!value)
      return ctx.Fail(CoachToolDecodeError::InvalidEnumValue,
                      "invalid on-loss mode: " + value_name);
    command.value = *value;
  }
  ctx.result.request = command;
  return true;
}

bool DecodeShout(Ctx& ctx, TeamSide self) {
  if (!CheckFields(ctx, {"type", "targets"})) return false;
  std::string type_name;
  if (!String(ctx, "type", type_name)) return false;
  const std::optional<ShoutType> type = ParseShoutType(type_name);
  if (!type)
    return ctx.Fail(CoachToolDecodeError::InvalidEnumValue,
                    "invalid shout type: " + type_name);

  const json* targets = Field(ctx, "targets");
  if (targets == nullptr)
    return ctx.Fail(CoachToolDecodeError::MissingField, "missing field: targets");
  if (!targets->is_array())
    return ctx.Fail(CoachToolDecodeError::WrongType, "targets must be an array");
  if (targets->empty())
    return ctx.Fail(CoachToolDecodeError::EmptyTargets, "targets must not be empty");
  if (targets->size() > kMaxShoutTargets)
    return ctx.Fail(CoachToolDecodeError::TooManyTargets,
                    "too many shout targets");

  IssueShout shout;
  shout.side = self;
  shout.type = *type;
  for (const json& entry : *targets) {
    const std::optional<PlayerId> target = AsPlayerId(entry);
    if (!target)
      return ctx.Fail(CoachToolDecodeError::ValueOutOfRange,
                      "targets must be valid player ids");
    shout.targets.push_back(*target);
  }
  ctx.result.request = shout;
  return true;
}

}  // namespace

CoachToolDecodeResult CoachToolDecoder::Decode(std::string_view tool_name,
                                               const json& arguments,
                                               TeamSide self) {
  Ctx ctx;
  ctx.object = &arguments;
  if (!RequireObject(ctx)) return ctx.result;

  bool ok = false;
  if (tool_name == "get_match_state") {
    ok = DecodeGetMatchState(ctx);
  } else if (tool_name == "get_tactical_board") {
    ok = DecodeGetTacticalBoard(ctx);
  } else if (tool_name == "get_player_info") {
    ok = DecodeGetPlayerInfo(ctx);
  } else if (tool_name == "set_team_instruction") {
    ok = DecodeSetTeamInstruction(ctx, self);
  } else if (tool_name == "set_slot_role") {
    ok = DecodeSetSlotRole(ctx, self);
  } else if (tool_name == "set_slot_anchor") {
    ok = DecodeSetSlotAnchor(ctx, self);
  } else if (tool_name == "set_individual_instruction") {
    ok = DecodeSetIndividualInstruction(ctx, self);
  } else if (tool_name == "set_transition_instruction") {
    ok = DecodeSetTransitionInstruction(ctx, self);
  } else if (tool_name == "shout_to_players") {
    ok = DecodeShout(ctx, self);
  } else {
    ctx.Fail(CoachToolDecodeError::UnknownTool,
             "unknown tool: " + std::string(tool_name));
    return ctx.result;
  }

  if (!ok) {
    ctx.result.request.reset();
    return ctx.result;
  }
  ctx.result.ok = true;
  ctx.result.error = CoachToolDecodeError::Ok;
  ctx.result.message = "ok";
  return ctx.result;
}

CoachToolDecodeResult CoachToolDecoder::DecodeJson(
    std::string_view tool_name, std::string_view arguments_json, TeamSide self) {
  const json parsed =
      json::parse(std::string(arguments_json), nullptr, false);
  if (parsed.is_discarded()) {
    CoachToolDecodeResult result;
    result.ok = false;
    result.error = CoachToolDecodeError::InvalidArguments;
    result.message = "arguments are not valid JSON";
    return result;
  }
  return Decode(tool_name, parsed, self);
}

}  // namespace football::ai
