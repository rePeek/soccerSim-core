#ifndef FOOTBALL_AI_ELIZA_INTENT_HPP
#define FOOTBALL_AI_ELIZA_INTENT_HPP

#include <array>
#include <algorithm>


#include "football/ai/tactical_board.hpp"
#include "football/ai/tactical_interpreter.hpp"
#include "football/observation/world_state.hpp"

namespace football::ai::eliza {

// Bounded, derived view of a V2 plan for the Eliza heuristic. It is a pure
// interpretation input, never a second authority: the plan stays in AIEngine,
// the interpreter rebuilds this every tick, and nothing writes it back.
//
// Legacy boards produce a default-constructed intent with has_plan == false, so
// the historical Eliza arithmetic stays bit-for-bit unchanged.
struct ElizaIntent {
  bool has_plan = false;
  float width = 0.75f;
  float defensive_line = 0.55f;
  float pressing_intensity = 0.5f;
  float tempo = 0.5f;
  float risk_taking = 0.5f;
  // +1 counter / counter-press, -1 hold shape / regroup, 0 balanced.
  float transition_on_win = 0.0f;
  float transition_on_loss = 0.0f;
};

// Candidate-scoring weights derived from the intent. Kept as a small value so
// the on-ball heuristic stays testable without running a match.
struct ElizaScoreWeights {
  // Sqrt(odds) needed to shoot. Lower = more willing to shoot.
  float shoot_odds_threshold = 0.30f;
  // Added to the pass release terms. Positive = releases sooner.
  float pass_release_shift = 0.0f;
  // Forward bias fraction on the long-ball target. Legacy value 0.2.
  float long_directness = 0.2f;
};

ElizaIntent MakeElizaIntent(const EffectiveTeamTactics& team);
ElizaScoreWeights MakeElizaScoreWeights(const ElizaIntent& intent);

// Bounded scaling of the off-ball defensive engagement. Identity for a legacy
// intent, so the historical arithmetic is preserved exactly.
inline float ElizaDefenceBias(float base_bias, const ElizaIntent& intent) {
  if (!intent.has_plan) return base_bias;
  return std::clamp(
      base_bias * (0.75f + 0.5f * std::clamp(intent.pressing_intensity, 0.0f, 1.0f)) +
          0.15f * intent.transition_on_loss,
      0.0f, 1.0f);
}

// Read-only legacy-shaped view of the interpreted plan, so the existing Eliza
// off-ball/defence code can consume it unchanged. Width/depth are identity
// (0.75/0.55) because the anchors are already in pitch metres.
TacticalBoard MakeElizaTacticalView(
    const EffectiveTeamTactics& team,
    const std::array<EffectivePlayerTactics, kTacticalSlotCount>& players,
    const WorldState& world, football::sim::TeamSide side);

}  // namespace football::ai::eliza

#endif  // FOOTBALL_AI_ELIZA_INTENT_HPP
