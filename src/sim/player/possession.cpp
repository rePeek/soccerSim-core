#include "sim/player/possession.hpp"

#include "sim/ball/ball.hpp"
#include "sim/player/player.hpp"
#include "sim/query/player_query.hpp"
#include "sim/team/team.hpp"

namespace football::sim::player {
void PrepareTeamPossession(Team& team, const Team& opponent, bool play_authorized,
                          bool set_piece_active, const Player* retainer,
                          const Team* best_possession_team) {
  float amount = (float)(opponent.GetTimeNeededToGetToBall_ms() + 1500) /
      (float)(team.GetTimeNeededToGetToBall_ms() + 1500);
  float fading = team.GetFadingTeamPossessionAmount();
  float tmpFadingTeamPossessionAmount =
      fading * 0.995f + clamp(amount, 0.5f, 1.5f) * 0.005f;
  fading += clamp(tmpFadingTeamPossessionAmount - fading, -0.005f, 0.005f);
  if (!play_authorized || set_piece_active || retainer != 0) {
    if (retainer != 0) {
      fading = amount = (retainer->GetTeam() == &team) ? 1.5f : 0.5f;
    } else {
      fading = amount = (best_possession_team == &team) ? 1.5f : 0.5f;
    }
  }
  team.SetPossessionAmounts(amount, fading);
}

void FinishTeamPossession(Team& team, const Team& opponent) {
  Player* designated = team.GetDesignatedTeamPossessionPlayer();
  int designatedPlayerTime_ms = designated->GetTimeNeededToGetToBall_ms();
  Player* bestPlayer = team.GetBestPossessionPlayer();
  int oppTime_ms = opponent.GetTimeNeededToGetToBall_ms();
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
    if (timeRating < 0.8f) team.SetDesignatedTeamPossessionPlayer(bestPlayer);
  }
}

void RefreshTeamPossession(Team& team, const Team& opponent, Ball& ball,
                          Tick now, const Player* retainer) {
  for (Player* actor : team.GetAllPlayers()) {
    if (actor->IsActive()) actor->UpdatePossessionStats(ball, opponent, now, retainer);
  }
  bool hasPossession = false;
  int timeNeededToGetToBall_ms = 100000;
  for (Player* actor : team.GetAllPlayers()) {
    if (actor->IsActive()) {
      if (actor->HasPossession()) hasPossession = true;
      if (actor->GetTimeNeededToGetToBall_ms() < timeNeededToGetToBall_ms)
        timeNeededToGetToBall_ms = actor->GetTimeNeededToGetToBall_ms();
    }
  }
  team.SetPossessionEstimate(hasPossession, timeNeededToGetToBall_ms);
}

void RefreshDesignatedTeamPossessionPlayer(Team& team, const Ball& ball) {
  team.SetDesignatedTeamPossessionPlayer(
      query::GetClosestPlayer(&team, ball.Predict(0).Get2D()));
}
} // namespace football::sim::player
