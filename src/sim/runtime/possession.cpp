#include "sim/runtime/possession.hpp"

#include <cassert>

#include "sim/player/player.hpp"

namespace football::sim {

PossessionSelection EvaluatePossession(TeamSide first_side,
                                      const TeamMatchState& first_state,
                                      TeamSide second_side,
                                      const TeamMatchState& second_state,
                                      Player* current_designated,
                                      Player* ball_retainer) {
  PossessionSelection selection{std::nullopt, current_designated};
  if (ball_retainer != 0) {
    selection.best_team =
        ball_retainer->GetTeamSide() == first_side ? first_side : second_side;
  } else {
    int bestTime_ms[2] = {100000, 100000};
    bestTime_ms[0] = first_state.time_to_ball_ms;
    bestTime_ms[1] = second_state.time_to_ball_ms;
    if (bestTime_ms[0] < bestTime_ms[1])
      selection.best_team = first_side;
    else if (bestTime_ms[0] > bestTime_ms[1])
      selection.best_team = second_side;
    else {
      assert(bestTime_ms[0] == bestTime_ms[1]);
      selection.best_team = std::nullopt;
    }
  }

  if (ball_retainer == 0) {
    const auto state_of = [&](TeamSide side) -> const TeamMatchState& {
      return side == first_side ? first_state : second_state;
    };
    if (selection.best_team.has_value()) {
      Player* candidate = state_of(*selection.best_team).designated_player;
      if (candidate != current_designated) {
        unsigned int designatedTime = current_designated->GetTimeNeededToGetToBall_ms();
        unsigned int candidateTime = candidate->GetTimeNeededToGetToBall_ms();
        float timeRating = (float)(candidateTime + 10) / (float)(designatedTime + 10);
        if (timeRating < 0.85f) selection.designated_player = candidate;
      }
    } else {
      // just stick with current team
      selection.designated_player =
          state_of(current_designated->GetTeamSide()).designated_player;
    }
  } else {
    selection.designated_player = ball_retainer;
  }
  return selection;
}

}  // namespace football::sim
