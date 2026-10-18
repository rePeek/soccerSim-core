#ifndef FOOTBALL_AI_COACH_TOOL_DECODER_HPP
#define FOOTBALL_AI_COACH_TOOL_DECODER_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "football/ai/coach_command.hpp"

namespace football::ai {

// Local, strict decoding of one model tool call into a typed request. The model
// output is untrusted: the arguments are validated field by field, the JSON
// type must match the named key, and only then is a CoachCommand built. This is
// the only place a tool call becomes game data; the board stays the single
// authority and the executor still validates against the live world.
//
// The controlling side is never supplied by the model: the Agent Session owns
// it and the decoder injects it, so a team can never instruct its opponent.

// Read requests. They resolve against the observed world/own board in the
// executor; they never mutate anything and never expose the opponent's plan.
struct GetMatchStateRequest {};
struct GetTacticalBoardRequest {};
struct GetPlayerInfoRequest {
  football::player::PlayerId player = football::player::kInvalidPlayerId;
};

// One decoded tool call: a read request or a typed mutating command.
using CoachToolRequest =
    std::variant<GetMatchStateRequest, GetTacticalBoardRequest,
                 GetPlayerInfoRequest, SetTeamInstruction, SetSlotRole,
                 SetSlotAnchor, SetIndividualInstruction,
                 SetTransitionInstruction, IssueShout>;

enum class CoachToolDecodeError : std::uint8_t {
  Ok = 0,
  UnknownTool,
  InvalidArguments,
  MissingField,
  UnknownField,
  WrongType,
  InvalidEnumValue,
  ValueOutOfRange,
  NonFiniteValue,
  EmptyTargets,
  TooManyTargets,
};

struct CoachToolDecodeResult {
  bool ok = false;
  CoachToolDecodeError error = CoachToolDecodeError::Ok;
  std::string message;
  // Present exactly when ok. A mutating request carries `side == self`.
  std::optional<CoachToolRequest> request;

  explicit operator bool() const { return ok; }
};

class CoachToolDecoder {
 public:
  // Validates one already-parsed arguments object against the named tool.
  static CoachToolDecodeResult Decode(std::string_view tool_name,
                                      const nlohmann::json& arguments,
                                      football::sim::TeamSide self);

  // Parses the raw model string first. A malformed JSON string is
  // InvalidArguments, never an exception.
  static CoachToolDecodeResult DecodeJson(std::string_view tool_name,
                                          std::string_view arguments_json,
                                          football::sim::TeamSide self);
};

}  // namespace football::ai

#endif  // FOOTBALL_AI_COACH_TOOL_DECODER_HPP
