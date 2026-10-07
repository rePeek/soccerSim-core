#ifndef FOOTBALL_SIM_MATCH_PHASE_HPP
#define FOOTBALL_SIM_MATCH_PHASE_HPP

// Referee-owned regulation phases. SecondHalf includes its kickoff preparation.
// Regulation time advances throughout an underway half, including ordinary
// dead balls, and pauses outside an underway half (opening/half-time ceremonies).
// No extra time/penalties yet.
enum class MatchPhase { PreMatch, FirstHalf, SecondHalf, Finished };

#endif  // FOOTBALL_SIM_MATCH_PHASE_HPP
