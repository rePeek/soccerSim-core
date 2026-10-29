#include "runtime/team_possession.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>

#include "football/ball/ball.hpp"
#include "player/execution/player_diagnostics.hpp"
#include "player/execution/player_locomotion.hpp"
#include "player/execution/player_decision_scheduler.hpp"
#include "player/execution/reachability.hpp"
#include "player/execution/roster_query.hpp"
#include "player/player.hpp"

using football::ball::Ball;

namespace football::sim {

namespace {

// Execute every 10 ms; refresh the intercept belief every 100 ms, staggered by
// the actor's runtime-owned phase. Between refreshes the estimate is retained.
constexpr football::sim::TickSpan kReachabilityRefresh{10};

// The public ETA is clamped so a saturated internal value never reads back
// negative or absurd; comparisons use the same clamp the Player getter had.
int ClampedEta(int eta_ms) { return std::min(eta_ms, 1000000); }

}  // namespace

void TeamMatchState::SetDesignatedPlayer(Player* player) {
  designated_actor = player;
  designated_player = player != nullptr ? player->GetID()
                                        : football::player::kInvalidPlayerId;
}

Player* BestPossessionPlayer(
    std::span<Player* const> players,
    std::span<const football::player::PlayerBallRelation> relations) {
  int bestTime_ms = 10000000;
  Player* bestPlayer = nullptr;
  for (std::size_t i = 0; i < players.size(); ++i) {
    if (!players[i]->IsActive()) continue;
    const int time_ms = ClampedEta(relations[i].time_to_ball_ms);
    if (time_ms < bestTime_ms) {
      bestTime_ms = time_ms;
      bestPlayer = players[i];
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
}

void FinishTeamPossession(
    std::span<Player* const> players,
    std::span<const football::player::PlayerBallRelation> relations,
    const TeamOrientation& frame, TeamMatchState& state,
    const TeamMatchState& opponent_state) {
  const auto relation_of = [&](const Player* player)
      -> const football::player::PlayerBallRelation& {
    for (std::size_t i = 0; i < players.size(); ++i) {
      if (players[i] == player) return relations[i];
    }
    static const football::player::PlayerBallRelation kUnknown;
    return kUnknown;
  };
  Player* designated = state.designated_actor;
  int designatedPlayerTime_ms =
      ClampedEta(relation_of(designated).time_to_ball_ms);
  Player* bestPlayer = BestPossessionPlayer(players, relations);
  int oppTime_ms = opponent_state.time_to_ball_ms;
  if (designated != bestPlayer) {
    int bestPlayerTime_ms = ClampedEta(relation_of(bestPlayer).time_to_ball_ms);
    float timeRating = (float)(bestPlayerTime_ms + 500) /
        (float)(designatedPlayerTime_ms + 500);
    if (relation_of(bestPlayer).locally_controllable) timeRating *= 0.5f;
    if (relation_of(designated).locally_controllable) timeRating /= 0.5f;
    if (designatedPlayerTime_ms < oppTime_ms - 100) {
      timeRating += 0.2f;
      timeRating *= 1.2f;
    }
    if (timeRating < 0.8f) {
      state.SetDesignatedPlayer(bestPlayer);
    }
  }
}

void RefreshTeamPossession(
    std::span<Player* const> players,
    std::span<football::player::PlayerBallRelation> relations,
    std::span<const std::uint8_t> phases, const TeamOrientation& frame,
    TeamMatchState& state, const TeamMatchState& opponent_state, Ball& ball,
    Tick now, const Player* retainer) {
  (void)frame;  // aggregation writes only estimates, which do not publish
  const Player* designated_own = state.designated_actor;
  for (std::size_t i = 0; i < players.size(); ++i) {
    Player* actor = players[i];
    if (!actor->IsActive()) continue;
    football::player::PlayerBallRelation& relation = relations[i];
    relation.previous_time_to_ball_ms = relation.time_to_ball_ms;

    // H3e1c-3c: solve on the staggered 100 ms cadence, retaining the capability
    // estimate between refreshes. Do not fall back to the legacy heuristic on
    // those nine intermediate ticks (P1c); that would change the AI's model.
    const e_FunctionType action_type = actor->GetCurrentFunctionType();
    const bool pure_locomotion =
        actor->GetSimulationActionState().IsPureLocomotion(retainer == actor);
    bool recompute = false;
    if (pure_locomotion) {
      ++PlayerReachabilityEligibleTicks();
      const bool scheduled_refresh =
          football::sim::player_timing::StaggeredRefreshDue(
              now, kReachabilityRefresh,
              football::sim::TickSpan{
                  static_cast<std::uint64_t>(phases[i])});
      // First eligible tick after an action must not reuse an action estimate.
      const bool entering_pure_locomotion =
          actor->GetSimulationActionState().elapsed.value == 0;
      recompute = scheduled_refresh || entering_pure_locomotion;
      if (recompute) ++PlayerReachabilityRefreshes();
      else ++PlayerReachabilityReuses();
    }

    // P19.4: the actor's own capability is one pure value. The cross-team
    // possession comparison below stays Runtime-side arbitration.
    football::sim::player::PlayerReachabilityInput reachability;
    reachability.kinematic = actor->GetKinematicState();
    reachability.max_velocity = actor->GetMaxVelocity();
    reachability.action_type = action_type;
    reachability.pure_locomotion = pure_locomotion;
    reachability.recompute = recompute;
    reachability.precise = designated_own == actor;
    reachability.touch_anim = actor->TouchAnim();
    reachability.touch_pending = actor->TouchPending();
    reachability.touch_frame = actor->GetTouchFrame();
    reachability.current_frame = actor->GetCurrentFrame();
    reachability.previous_usual_ms =
        static_cast<unsigned int>(relation.time_to_ball_ms);
    reachability.previous_optimistic_ms =
        static_cast<unsigned int>(relation.optimistic_ms);
    reachability.ball = &ball;
    const football::sim::player::PlayerReachabilityFacts facts =
        football::sim::player::ComputePlayerReachability(reachability);
    relation.time_to_ball_ms = static_cast<int>(facts.usual_ms);
    relation.optimistic_ms = static_cast<int>(facts.optimistic_ms);
    relation.locally_controllable = facts.locally_controllable;
    relation.best_in_team =
        relation.locally_controllable &&
        opponent_state.time_to_ball_ms > ClampedEta(relation.time_to_ball_ms);
    relation.unique_in_match =
        relation.locally_controllable && !opponent_state.has_possession;
    if (retainer == actor) {
      relation.time_to_ball_ms = 1;
      relation.optimistic_ms = 1;
      actor->SetDesiredTimeToBall_ms(relation.time_to_ball_ms);
      relation.locally_controllable = true;
      relation.best_in_team = true;
      relation.unique_in_match = true;
    } else if (retainer != nullptr) {
      relation.locally_controllable = false;
      relation.best_in_team = false;
      relation.unique_in_match = false;
    }
  }

  bool hasPossession = false;
  int timeNeededToGetToBall_ms = 100000;
  for (std::size_t i = 0; i < players.size(); ++i) {
    if (!players[i]->IsActive()) continue;
    if (relations[i].locally_controllable) hasPossession = true;
    const int eta = ClampedEta(relations[i].time_to_ball_ms);
    if (eta < timeNeededToGetToBall_ms) timeNeededToGetToBall_ms = eta;
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
}

}  // namespace football::sim
