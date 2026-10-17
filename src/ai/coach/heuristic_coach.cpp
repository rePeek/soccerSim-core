#include "heuristic_coach.hpp"

#include <cmath>

namespace football::ai {
namespace {
void SetNumber(const CoachContext& context, InstructionKey key, float current,
               float target, std::vector<CoachCommand>& out) {
  if (std::abs(current - target) < 1.0e-3f) return;
  SetTeamInstruction command;
  command.side = context.self;
  command.key = key;
  command.value = target;
  out.push_back(command);
}

void SetTransition(const CoachContext& context, TransitionKey key,
                   TransitionValue current, TransitionValue target,
                   std::vector<CoachCommand>& out) {
  if (current == target) return;
  SetTransitionInstruction command;
  command.side = context.self;
  command.key = key;
  command.value = target;
  out.push_back(command);
}
}  // namespace

void HeuristicCoachPolicy::RunTurn(const CoachContext& context,
                                   std::vector<CoachCommand>& out) {
  out.clear();
  if (!context.current_tactics) return;  // legacy board: nothing to edit
  const TacticalPlanV2& plan = *context.current_tactics;
  const int difference = context.self_score - context.opponent_score;

  const bool chase = difference < 0 && context.phase == MatchPhase::SecondHalf;
  const bool protect = difference >= 2 && context.phase == MatchPhase::SecondHalf;
  if (!chase && !protect) return;  // NoOp

  const InPossessionInstructions& ip = plan.in_possession;
  const OutOfPossessionInstructions& oop = plan.out_of_possession;
  if (chase) {
    SetNumber(context, InstructionKey::Tempo, ip.tempo, 0.70f, out);
    SetNumber(context, InstructionKey::AttackingBias, ip.attacking_bias, 0.75f, out);
    SetNumber(context, InstructionKey::RiskTaking, ip.risk_taking, 0.65f, out);
    SetNumber(context, InstructionKey::DefensiveLine, oop.defensive_line, 0.62f, out);
    SetNumber(context, InstructionKey::LineOfEngagement, oop.line_of_engagement,
              0.70f, out);
    SetNumber(context, InstructionKey::PressingIntensity, oop.pressing_intensity,
              0.68f, out);
    SetTransition(context, TransitionKey::OnWin, plan.transition.on_win,
                  TransitionOnWin::Counter, out);
    SetTransition(context, TransitionKey::OnLoss, plan.transition.on_loss,
                  TransitionOnLoss::CounterPress, out);
  } else {
    SetNumber(context, InstructionKey::Tempo, ip.tempo, 0.40f, out);
    SetNumber(context, InstructionKey::AttackingBias, ip.attacking_bias, 0.40f, out);
    SetNumber(context, InstructionKey::RiskTaking, ip.risk_taking, 0.35f, out);
    SetNumber(context, InstructionKey::DefensiveLine, oop.defensive_line, 0.42f, out);
    SetNumber(context, InstructionKey::LineOfEngagement, oop.line_of_engagement,
              0.40f, out);
    SetNumber(context, InstructionKey::Compactness, oop.compactness, 0.62f, out);
    SetTransition(context, TransitionKey::OnLoss, plan.transition.on_loss,
                  TransitionOnLoss::Regroup, out);
  }
}

}  // namespace football::ai
