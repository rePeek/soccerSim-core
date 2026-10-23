#include "runtime/team_possession.hpp"

#include <cassert>

#include "football/ball/ball.hpp"
#include "player/player.hpp"
#include "player/roster_query.hpp"
#include "runtime/team_facts.hpp"

using football::ball::Ball;

namespace football::sim {

void TeamMatchState::SetDesignatedPlayer(Player* player) {
  designated_actor = player;
  designated_player = player != nullptr ? player->GetID()
                                        : football::player::kInvalidPlayerId;
}

Player* BestPossessionPlayer(std::span<Player* const> players) {
  int bestTime_ms = 10000000;
  Player* bestPlayer = nullptr;
  for (Player* player : players) {
    if (player->IsActive()) {
      const int time_ms = player->GetTimeNeededToGetToBall_ms();
      if (time_ms < bestTime_ms) {
        bestTime_ms = time_ms;
        bestPlayer = player;
      }
    }
  }
  assert(bestPlayer != nullptr);
  return bestPlayer;
}

void PrepareTeamPossession(TeamSide side, std::span<Player* const> players,
                          const TeamOrientation& frame, TeamMatchState& state,
                          const TeamMatchState& opponent_state,
                          bool play_authorized, bool set_piece_active,
                          const Player* retainer,
                          std::optional<TeamSide> best_possession_team) {
  float amount = (float)(opponent_state.time_to_ball_ms + 1500) /
      (float)(state.time_to_ball_ms + 1500);
  float fading = state.fading_possession_amount;
  float tmpFadingTeamPossessionAmount =
      fading * 0.995f + clamp(amount, 0.5f, 1.5f) * 0.005f;
  fading += clamp(tmpFadingTeamPossessionAmount - fading, -0.005f, 0.005f);
  if (!play_authorized || set_piece_active || retainer != 0) {
    if (retainer != 0) {
      fading = amount = (retainer->GetTeamSide() == side) ? 1.5f : 0.5f;
    } else {
      fading = amount =
          (best_possession_team.has_value() && *best_possession_team == side) ? 1.5f : 0.5f;
    }
  }
  state.possession_amount = amount;
  state.fading_possession_amount = fading;
  PublishTeamFacts(players, frame, state);
}

void FinishTeamPossession(std::span<Player* const> players,
                          const TeamOrientation& frame, TeamMatchState& state,
                          const TeamMatchState& opponent_state) {
  Player* designated = state.designated_actor;
  int designatedPlayerTime_ms = designated->GetTimeNeededToGetToBall_ms();
  Player* bestPlayer = BestPossessionPlayer(players);
  int oppTime_ms = opponent_state.time_to_ball_ms;
  if (designated != bestPlayer) {
    int bestPlayerTime_ms = bestPlayer->GetTimeNeededToGetToBall_ms();
    float timeRating = (float)(bestPlayerTime_ms + 500) /
        (float)(designatedPlayerTime_ms + 500);
    if (bestPlayer->HasPossession()) timeRating *= 0.5f;
    if (designated->HasPossession()) timeRating /= 0.5f;
    if (designatedPlayerTime_ms < oppTime_ms - 100) {
      timeRating += 0.2f;
      timeRating *= 1.2f;
    }
    if (timeRating < 0.8f) {
      state.SetDesignatedPlayer(bestPlayer);
      PublishTeamFacts(players, frame, state);
    }
  }
}

void RefreshTeamPossession(std::span<Player* const> players,
                          const TeamOrientation& frame, TeamMatchState& state,
                          const TeamMatchState& opponent_state, Ball& ball,
                          Tick now, const Player* retainer) {
  (void)frame;  // aggregation writes only estimates, which do not publish
  for (Player* actor : players) {
    if (actor->IsActive()) actor->UpdatePossessionStats(
        ball, opponent_state, now, retainer, state.designated_actor);
  }
  bool hasPossession = false;
  int timeNeededToGetToBall_ms = 100000;
  for (Player* actor : players) {
    if (actor->IsActive()) {
      if (actor->HasPossession()) hasPossession = true;
      if (actor->GetTimeNeededToGetToBall_ms() < timeNeededToGetToBall_ms)
        timeNeededToGetToBall_ms = actor->GetTimeNeededToGetToBall_ms();
    }
  }
  state.has_possession = hasPossession;
  state.time_to_ball_ms = timeNeededToGetToBall_ms;
}

void RefreshDesignatedTeamPossessionPlayer(std::span<Player* const> players,
                                          const TeamOrientation& frame,
                                          TeamMatchState& state,
                                          const Ball& ball) {
  state.SetDesignatedPlayer(
      football::sim::player::FindClosestPlayer(players, ball.state().position.Get2D()));
  PublishTeamFacts(players, frame, state);
}

}  // namespace football::sim
