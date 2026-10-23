#ifndef FOOTBALL_SIM_RULES_FOUL_SANCTION_HPP
#define FOOTBALL_SIM_RULES_FOUL_SANCTION_HPP

#include "football/player/foul_assessment.hpp"

namespace football::sim::rules {

// IFAB Law 12 challenge severity. Careless is a foul, reckless is a caution and
// excessive force is a send-off. It is derived from the frozen challenge
// intensity the contact solver already measured, never from a probability.
enum class FoulSeverity { None, Careless, Reckless, Excessive };

// Deterministic sanction decision for one confirmed foul. The double-jeopardy
// downgrade (a penalty plus an attempt to play the ball is a caution, not a
// send-off) is applied by the caller that knows the offender's penalty area.
struct FoulSanction {
  bool foul = false;
  FoulSeverity severity = FoulSeverity::None;
  bool dogso = false;
  // 0 = no offence, 1 = foul only, 2 = caution, 3 = send-off.
  int card = 0;
};

// Pure classifier: challenge kind, frozen severity and whether the foul denied
// an obvious goal-scoring opportunity. `dogso` forces the send-off card; the
// penalty-area downgrade is a separate, location-aware step.
FoulSanction ClassifyFoul(FoulKind kind, float severity, bool dogso);

}  // namespace football::sim::rules

#endif  // FOOTBALL_SIM_RULES_FOUL_SANCTION_HPP
