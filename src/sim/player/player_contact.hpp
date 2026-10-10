#ifndef FOOTBALL_SIM_PLAYER_CONTACT_HPP
#define FOOTBALL_SIM_PLAYER_CONTACT_HPP

#include <optional>
#include <span>
#include <vector>

#include "foundation/time/tick.hpp"
#include "sim/player/foul_assessment.hpp"

namespace football::ball { class Ball; }
class Player;

namespace football::sim {

struct PlayerContactInputs {
  Tick now;
  // Active players in processing-roster order, sharing the ball's physical frame.
  std::span<Player* const> players;
  const football::ball::Ball& ball;
  Player* designated_possession_player;
  Player* ball_retainer;
};

// Pure per-collision foul attribution. `first_sensitivity`/`second_sensitivity`
// are the fall sensitivities the solver already computed for this pair, so the
// assessment never re-runs collision math. At most one suspected offender is
// returned; harmless contacts and same-team contacts return nullopt. Both
// actors may still fall physically: the fall and the foul are independent.
// Every rule-relevant field is frozen here from the live contact instant.
std::optional<FoulAssessment> AssessCollision(Player* first, Player* second,
                                              float first_sensitivity,
                                              float second_sensitivity,
                                              const football::ball::Ball& ball,
                                              Tick now);

// Mutates players in pair order, then applies accumulated movement sharing.
// Physical falls are not foul verdicts; the solver only collects one
// FoulAssessment per suspicious collision for the referee to settle.
void ResolvePlayerContacts(const PlayerContactInputs& inputs,
                           std::vector<FoulAssessment>& assessments);

}  // namespace football::sim

#endif  // FOOTBALL_SIM_PLAYER_CONTACT_HPP
