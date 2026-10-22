#include "coach_tool_json.hpp"

#include <string>

#include "coach_tool_decoder.hpp"
#include "coach_tool_names.hpp"

namespace football::ai {
namespace {

using nlohmann::json;

json VectorJson(const football::math::Vector3& value) {
  return json::array({value.coords[0], value.coords[1], value.coords[2]});
}

json AnchorJson(const TacticalAnchor& anchor) {
  return json{{"u", anchor.u}, {"v", anchor.v}};
}

json TrendJson(const TeamTrendSummary& trend) {
  return json{{"samples", trend.sample_count},
              {"possession_fraction", trend.possession_fraction},
              {"mean_ball_x_fraction", trend.mean_ball_x_fraction},
              {"ball_in_play_fraction", trend.ball_in_play_fraction}};
}

json IndividualJson(const IndividualInstructions& instructions) {
  json individual = json::object();
  if (instructions.forward_runs)
    individual["forward_runs"] = *instructions.forward_runs;
  if (instructions.pressing) individual["pressing"] = *instructions.pressing;
  if (instructions.passing_risk)
    individual["passing_risk"] = *instructions.passing_risk;
  if (instructions.dribbling) individual["dribbling"] = *instructions.dribbling;
  if (instructions.positional_freedom)
    individual["positional_freedom"] = *instructions.positional_freedom;
  if (instructions.marking_target)
    individual["marking_target"] = *instructions.marking_target;
  return individual;
}

}  // namespace

nlohmann::json MatchStateJson(const CoachMatchStateSummary& summary) {
  return json{{"tick", summary.tick},
              {"phase", std::string(PhaseWireName(summary.phase))},
              {"self_score", summary.self_score},
              {"opponent_score", summary.opponent_score},
              {"has_possession", summary.has_possession},
              {"ball_in_play", summary.ball_in_play},
              {"restart_pending", summary.restart_pending}};
}

nlohmann::json TacticalPlanJson(const TacticalPlan& plan) {
  json out;
  out["mentality"] = std::string(ToWireName(plan.mentality));

  const InPossessionInstructions& in_possession = plan.in_possession;
  out["in_possession"] =
      json{{"attacking_width", in_possession.attacking_width},
           {"tempo", in_possession.tempo},
           {"passing_directness", in_possession.passing_directness},
           {"risk_taking", in_possession.risk_taking},
           {"creative_freedom", in_possession.creative_freedom},
           {"attacking_bias", in_possession.attacking_bias},
           {"build_up", std::string(ToWireName(in_possession.build_up))},
           {"pass_into_space", in_possession.pass_into_space}};

  const OutOfPossessionInstructions& out_of_possession = plan.out_of_possession;
  out["out_of_possession"] =
      json{{"defensive_line", out_of_possession.defensive_line},
           {"line_of_engagement", out_of_possession.line_of_engagement},
           {"pressing_intensity", out_of_possession.pressing_intensity},
           {"compactness", out_of_possession.compactness},
           {"marking", std::string(ToWireName(out_of_possession.marking))},
           {"offside_trap", out_of_possession.offside_trap}};

  out["transition"] =
      json{{"on_win", std::string(ToWireName(plan.transition.on_win))},
           {"on_loss", std::string(ToWireName(plan.transition.on_loss))}};

  json slots = json::array();
  for (const TacticalSlot& slot : plan.slots) {
    slots.push_back(
        json{{"slot", slot.id},
             {"in_possession_role",
              std::string(ToWireName(slot.in_possession_role))},
             {"out_of_possession_role",
              std::string(ToWireName(slot.out_of_possession_role))},
             {"in_possession_anchor", AnchorJson(slot.in_possession)},
             {"out_of_possession_anchor", AnchorJson(slot.out_of_possession)},
             {"individual", IndividualJson(slot.instructions)}});
  }
  out["slots"] = std::move(slots);
  return out;
}

nlohmann::json PlayerInfoJson(const CoachPlayerInfo& info) {
  return json{{"player", info.id},
              {"active", info.active},
              {"has_possession", info.has_possession},
              {"role", std::string(RoleWireName(info.role))},
              {"position", VectorJson(info.position)}};
}

nlohmann::json CoachContextJson(const CoachContext& context) {
  json out;
  out["self"] = context.self == football::sim::TeamSide::Home ? "home" : "away";
  out["observed_tick"] = context.observed_tick;
  out["board_revision"] = context.board_revision;
  out["phase"] = std::string(PhaseWireName(context.phase));
  out["score"] =
      json{{"self", context.self_score}, {"opponent", context.opponent_score}};
  out["clock"] = json{{"regulation_ticks", context.clock.regulation_ticks},
                      {"half_underway", context.clock.half_underway},
                      {"ball_in_play", context.clock.ball_in_play}};
  out["last_5_min"] = TrendJson(context.last_5_min);
  out["last_15_min"] = TrendJson(context.last_15_min);

  json players = json::array();
  for (const PlayerSummary& player : context.own_players) {
    players.push_back(json{{"player", player.id},
                           {"role", std::string(RoleWireName(player.role))},
                           {"position", VectorJson(player.position)},
                           {"active", player.active},
                           {"has_possession", player.has_possession},
                           {"fatigue_inv", player.fatigue_inv}});
  }
  out["players"] = std::move(players);

  json events = json::array();
  for (const MatchEventSummary& event : context.recent_events) {
    events.push_back(
        json{{"type", std::string(CoachEventWireName(event.type))},
             {"side", event.side == context.self ? "self" : "opponent"},
             {"tick", event.tick}});
  }
  out["recent_events"] = std::move(events);

  out["current_tactics"] = TacticalPlanJson(context.current_tactics);

  json shouts = json::array();
  for (const ActiveShout& shout : context.active_shouts) {
    shouts.push_back(json{{"player", shout.target},
                          {"type", std::string(ToWireName(shout.type))},
                          {"issued_tick", shout.issued_tick},
                          {"expires_at_tick", shout.expires_at_tick}});
  }
  out["active_shouts"] = std::move(shouts);
  return out;
}

std::string_view PhaseWireName(MatchPhase phase) {
  switch (phase) {
    case MatchPhase::PreMatch: return "pre_match";
    case MatchPhase::FirstHalf: return "first_half";
    case MatchPhase::SecondHalf: return "second_half";
    case MatchPhase::Finished: return "finished";
  }
  return "unknown";
}

std::string_view RoleWireName(e_PlayerRole role) {
  switch (role) {
    case e_PlayerRole_GK: return "GK";
    case e_PlayerRole_CB: return "CB";
    case e_PlayerRole_LB: return "LB";
    case e_PlayerRole_RB: return "RB";
    case e_PlayerRole_DM: return "DM";
    case e_PlayerRole_CM: return "CM";
    case e_PlayerRole_LM: return "LM";
    case e_PlayerRole_RM: return "RM";
    case e_PlayerRole_AM: return "AM";
    case e_PlayerRole_CF: return "CF";
  }
  return "unknown";
}

std::string_view CoachEventWireName(CoachEventType type) {
  switch (type) {
    case CoachEventType::KickOff: return "kick_off";
    case CoachEventType::Goal: return "goal";
    case CoachEventType::Card: return "card";
    case CoachEventType::Foul: return "foul";
    case CoachEventType::Offside: return "offside";
    case CoachEventType::Restart: return "restart";
    case CoachEventType::BallOutOfPlay: return "ball_out_of_play";
    case CoachEventType::BallInPlay: return "ball_in_play";
    case CoachEventType::PeriodChange: return "period_change";
  }
  return "unknown";
}

std::string_view DecodeErrorWireName(CoachToolDecodeError error) {
  switch (error) {
    case CoachToolDecodeError::Ok: return "ok";
    case CoachToolDecodeError::UnknownTool: return "unknown_tool";
    case CoachToolDecodeError::InvalidArguments: return "invalid_arguments";
    case CoachToolDecodeError::MissingField: return "missing_field";
    case CoachToolDecodeError::UnknownField: return "unknown_field";
    case CoachToolDecodeError::WrongType: return "wrong_type";
    case CoachToolDecodeError::InvalidEnumValue: return "invalid_enum";
    case CoachToolDecodeError::ValueOutOfRange: return "out_of_range";
    case CoachToolDecodeError::NonFiniteValue: return "non_finite";
    case CoachToolDecodeError::EmptyTargets: return "empty_targets";
    case CoachToolDecodeError::TooManyTargets: return "too_many_targets";
  }
  return "unknown";
}

}  // namespace football::ai
