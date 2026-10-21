#include "referee/foul_sanction.hpp"

namespace football::sim::rules {

namespace {

// The legacy challenge-intensity scale, now named instead of left as bare
// thresholds. A standing fall is always the low end: the solver only reports
// one for a genuine challenge near the ball.
FoulSeverity SeverityOf(FoulKind kind, float severity) {
  if (kind == FoulKind::StandingFall) return FoulSeverity::Careless;
  if (severity <= 1.0f) return FoulSeverity::None;
  if (severity > 2.0f) return FoulSeverity::Excessive;
  if (severity > 1.4f) return FoulSeverity::Reckless;
  return FoulSeverity::Careless;
}

int CardOf(FoulSeverity severity) {
  switch (severity) {
    case FoulSeverity::None: return 0;
    case FoulSeverity::Careless: return 1;
    case FoulSeverity::Reckless: return 2;
    case FoulSeverity::Excessive: return 3;
  }
  return 0;
}

}  // namespace

FoulSanction ClassifyFoul(FoulKind kind, float severity, bool dogso) {
  FoulSanction result;
  result.severity = SeverityOf(kind, severity);
  result.foul = result.severity != FoulSeverity::None;
  if (!result.foul) return result;
  result.dogso = dogso;
  result.card = CardOf(result.severity);
  // Denying an obvious goal-scoring opportunity is a send-off on its own. The
  // double-jeopardy downgrade needs the penalty-area location the caller owns.
  if (dogso) result.card = 3;
  return result;
}

}  // namespace football::sim::rules
