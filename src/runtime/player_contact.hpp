#ifndef FOOTBALL_SIM_PLAYER_CONTACT_HPP
#define FOOTBALL_SIM_PLAYER_CONTACT_HPP

#include <optional>
#include <span>
#include <vector>

#include "football/foundation/time/tick.hpp"
#include "football/contact/foul_assessment.hpp"
#include "runtime/player_ball_table.hpp"

namespace football::ball { class Ball; }
class Player;

namespace football::sim {

struct TeamMatchState;

struct PlayerContactInputs {
  Tick now;
  // Active players in processing-team order, sharing the ball's physical frame.
  std::span<Player* const> players;
  const football::ball::Ball& ball;
  Player* designated_possession_player;
  Player* ball_retainer;
  // Borrowed Runtime-owned team state per team id; null when a caller has none.
  // Rule evidence (possession weight, designated actor) is read from here, not
  // from a per-Player cache.
  const TeamMatchState* team_states[2] = {nullptr, nullptr};
  // Runtime-owned ball relations; rule evidence reads them, never the Player.
  const PlayerBallTable* ball_table = nullptr;
};

// Runtime adapter for per-collision evidence capture. `first_sensitivity` and
// `second_sensitivity` are the solver's already-computed fall sensitivities; the
// assessment never re-runs collision math. At most one suspected offender is
// returned; harmless contacts and same-team contacts return nullopt. Both
// actors may still fall physically: the fall and the foul are independent.
// Every rule-relevant field is frozen here from the live contact instant.
std::optional<FoulAssessment> AssessCollision(Player* first, Player* second,
                                              float first_sensitivity,
                                              float second_sensitivity,
                                              const football::ball::Ball& ball,
                                              Tick now,
                                              const TeamMatchState* const* team_states);

// Mutates players in pair order, then applies accumulated movement sharing.
// Physical falls are not foul verdicts; the solver only collects one
// FoulAssessment per suspicious collision for the referee to settle.
void ResolvePlayerContacts(const PlayerContactInputs& inputs,
                           std::vector<FoulAssessment>& assessments);

}  // namespace football::sim

#endif  // FOOTBALL_SIM_PLAYER_CONTACT_HPP
