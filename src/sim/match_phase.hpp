#ifndef FOOTBALL_SIM_MATCH_PHASE_HPP
#define FOOTBALL_SIM_MATCH_PHASE_HPP

// Referee-owned regulation phases. SecondHalf includes its kickoff preparation;
// the match clock is paused whenever play is stopped. No extra time/penalties yet.
enum class MatchPhase { PreMatch, FirstHalf, SecondHalf, Finished };

#endif  // FOOTBALL_SIM_MATCH_PHASE_HPP
