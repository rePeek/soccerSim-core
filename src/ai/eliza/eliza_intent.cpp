#include "eliza_intent.hpp"

#include <algorithm>

namespace football::ai::eliza {
namespace {
using football::ai::TransitionOnLoss;
using football::ai::TransitionOnWin;
}  // namespace

ElizaIntent MakeElizaIntent(const EffectiveTeamTactics& team) {
  ElizaIntent intent;
  intent.width = team.width;
  intent.defensive_line = team.defensive_line;
  intent.pressing_intensity = team.press;
  intent.tempo = team.tempo;
  intent.risk_taking = team.risk;
  switch (team.on_win) {
    case TransitionOnWin::Counter: intent.transition_on_win = 1.0f; break;
    case TransitionOnWin::HoldShape: intent.transition_on_win = -1.0f; break;
    case TransitionOnWin::Balanced: intent.transition_on_win = 0.0f; break;
  }
  switch (team.on_loss) {
    case TransitionOnLoss::CounterPress: intent.transition_on_loss = 1.0f; break;
    case TransitionOnLoss::Regroup: intent.transition_on_loss = -1.0f; break;
    case TransitionOnLoss::Balanced: intent.transition_on_loss = 0.0f; break;
  }
  return intent;
}

ElizaScoreWeights MakeElizaScoreWeights(const ElizaIntent& intent) {
  ElizaScoreWeights weights;
  // risk 0 -> 0.42 (conservative), risk 1 -> 0.18 (aggressive); 0.5 -> 0.30.
  weights.shoot_odds_threshold =
      0.30f - 0.24f * (intent.risk_taking - 0.5f);
  // tempo above neutral releases sooner; below neutral holds the ball.
  weights.pass_release_shift = intent.tempo - 0.5f;
  // Higher risk and a counter transition both push the long ball forward.
  weights.long_directness =
      0.2f + 0.15f * intent.risk_taking + 0.1f * std::max(0.0f, intent.transition_on_win);
  return weights;
}

}  // namespace football::ai::eliza
