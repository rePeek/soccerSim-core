#ifndef FOOTBALL_SIM_MATCH_RESULT_HPP
#define FOOTBALL_SIM_MATCH_RESULT_HPP

#include <cstdint>

enum class MatchOutcome { HomeWin, AwayWin, Draw };

// Owning final value, available only after the referee's full-time whistle.
struct MatchResult {
  int home_score = 0;
  int away_score = 0;
  MatchOutcome outcome = MatchOutcome::Draw;
  // Actual executed simulation steps, not compressed restart/elapsed clock units.
  std::uint64_t duration_ticks = 0;
  friend bool operator==(const MatchResult&, const MatchResult&) = default;
};

#endif  // FOOTBALL_SIM_MATCH_RESULT_HPP
