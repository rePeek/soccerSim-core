#include "sim/team/possession.hpp"

#include <cassert>

#include "sim/player/player.hpp"
#include "sim/team/team.hpp"

namespace football::sim {

PossessionSelection EvaluatePossession(Team& first, Team& second,
                                      Player* current_designated,
                                      Player* ball_retainer) {
  PossessionSelection selection{nullptr, current_designated};
  if (ball_retainer != 0) {
    selection.best_team = ball_retainer->GetTeam();
  } else {
    int bestTime_ms[2] = {100000, 100000};
    bestTime_ms[0] = first.GetTimeNeededToGetToBall_ms();
    bestTime_ms[1] = second.GetTimeNeededToGetToBall_ms();
    if (bestTime_ms[0] < bestTime_ms[1])
      selection.best_team = &first;
    else if (bestTime_ms[0] > bestTime_ms[1])
      selection.best_team = &second;
    else {
      assert(bestTime_ms[0] == bestTime_ms[1]);
      selection.best_team = 0;
    }
  }

  if (ball_retainer == 0) {
    if (selection.best_team) {
      Player* candidate = selection.best_team->GetDesignatedTeamPossessionPlayer();
      if (candidate != current_designated) {
        unsigned int designatedTime = current_designated->GetTimeNeededToGetToBall_ms();
        unsigned int candidateTime = candidate->GetTimeNeededToGetToBall_ms();
        float timeRating = (float)(candidateTime + 10) / (float)(designatedTime + 10);
        if (timeRating < 0.85f) selection.designated_player = candidate;
      }
    } else {
      // just stick with current team
      selection.designated_player = current_designated->GetTeam()->GetDesignatedTeamPossessionPlayer();
    }
  } else {
    selection.designated_player = ball_retainer;
  }
  return selection;
}

}  // namespace football::sim
