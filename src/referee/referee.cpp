// Copyright 2019 Google LLC & Bastiaan Konings
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not be used for anything important.
// i do not offer support, so don't ask. to be used for inspiration :)

#include "referee/referee.hpp"
#include "football/pitch/pitch.hpp"
#include <cmath>
#include <type_traits>
#include <algorithm>

#include "referee/offside.hpp"
#include "football/animation/types.hpp"
#include "referee/ball_rules.hpp"
#include "football/pitch/pitch_frame.hpp"

namespace {
using football::sim::Tick;
using football::sim::BallOutOfPlay;
using football::sim::ClassifyBallOutOfPlay;
using football::sim::FoulAssessment;
using football::sim::FoulKind;
using football::sim::TickSpan;
using football::sim::Seconds;
using football::sim::rules::RefereeTickFacts;
using football::sim::rules::RefereeTeamFact;
constexpr auto kCardAdministration = Seconds(10);
constexpr TickSpan kTouchGrace{60};
constexpr TickSpan kAdvantageRecheck{60};
constexpr auto kAdvantageExpiry = Seconds(3);
constexpr auto kCardEffectDelay = Seconds(6);

// Assemble the live team rule facts a foul verdict reads this instant.
FoulLiveFacts LiveFoulFacts(const RefereeTeamFact& a, const RefereeTeamFact& b) {
  FoulLiveFacts live;
  const RefereeTeamFact* teams[2] = {&a, &b};
  for (const RefereeTeamFact* team : teams) {
    FoulTeamFacts facts;
    facts.team_id = team->team_id;
    facts.defending_direction = team->defending_direction;
    facts.fading_possession_amount = team->fading_possession_amount;
    if (team->team_id == 0) live.home = facts;
    else live.away = facts;
  }
  return live;
}

}  // namespace

Referee::Referee() {
  foul.offender = football::player::kInvalidPlayerId;
  foul.victim = football::player::kInvalidPlayerId;
  foul.offender_team_id = -1;
  foul.victim_team_id = -1;
  foul.foulType = 0;
  foul.advantage = false;
  foul.foul_tick = {};
  foul.hasBeenProcessed = true;
}

Referee::~Referee() {}

void Referee::OnCeremonialSetup() {
  // A placed/repositioned setup invalidates the last-touch offside reference.
  offsidePlayers.clear();
}

void Referee::OnRestartTaken() {
  // The awarded restart completed; abandon the foul that produced it.
  foul.offender = football::player::kInvalidPlayerId;
  foul.victim = football::player::kInvalidPlayerId;
  foul.foulType = 0;
}

void Referee::OnPeriodEnded(MatchPhase ended_phase) {
  // Full time keeps the terminal rule state; play/restart lifecycle belongs to
  // Runtime. A half-time break abandons a pending foul.
  if (ended_phase == MatchPhase::SecondHalf) return;
  foul.offender = football::player::kInvalidPlayerId;
  foul.victim = football::player::kInvalidPlayerId;
  foul.foulType = 0;
  foul.advantage = false;
  foul.foul_tick = {};
  foul.hasBeenProcessed = true;
}

void Referee::SubmitRestart(e_GameMode kind, football::sim::TeamSide team,
                            const football::math::Vector3& home_position,
                            TickSpan administration,
                            football::sim::event::RulingSink& rulings,
                            const football::sim::event::RestartCause& cause) {
  // The verdict is authoritative; Runtime creates and owns the restart context
  // and publishes the confirmed award (and foul/offside cause) itself.
  rulings.Submit(football::sim::event::AwardRestartRuling{
      kind, team, home_position, administration, cause});
}

void Referee::EvaluateOutOfPlay(const football::sim::rules::RefereeView& view,
                                football::sim::event::RulingSink& rulings) {
  const RefereeTickFacts& tick = view.tick;
  if (tick.phase == MatchPhase::Finished) return;
  if (!(tick.play_authorized && !tick.set_piece_active)) return;
  const auto home_ball = tick.ball_to_home.Position(tick.ball.position);
  // Legacy foul/side data uses stadium coordinates, including switched ends.
  const football::math::Vector3 ballPos = tick.stadium_to_home.Position(home_ball);
  // Goal-line priority: the legacy local authorization was cleared when the goal
  // line applied, so the sideline branch could not run at the same instant.
  const BallOutOfPlay out = ClassifyBallOutOfPlay(tick.pitch, ballPos);
  if (out == BallOutOfPlay::GoalLine || tick.goal_scored) {
    SettleGoalLine(ballPos, tick, rulings);
  } else if (out == BallOutOfPlay::Touchline) {
    SettleTouchline(ballPos, tick, view.post_restart_grace, rulings);
  }
}

void Referee::SettleGoalLine(const football::math::Vector3& ballPos,
                             const RefereeTickFacts& facts,
                             football::sim::event::RulingSink& rulings) {
  const auto team = [&](int id) -> const RefereeTeamFact& {
    return id == 0 ? facts.home : facts.away;
  };
  foul.advantage = false;
  bool isFoul = false;
  if (!facts.goal_scored) {
    isFoul = CheckFoul(facts.now, facts.pitch, facts.stadium_to_home,
                       LiveFoulFacts(facts.home, facts.away), rulings);
  } else {
    foul.foulType = 0;
  }
  if (isFoul) return;
  rulings.Submit(football::sim::event::StopPlayRuling{});
  // corner, goal kick or kick off?
  int lastTouchTeam = facts.last_touch_team;
  if (lastTouchTeam < 0) lastTouchTeam = facts.first_team;
  signed int lastSide = team(lastTouchTeam).defending_direction;
  e_GameMode kind = e_GameMode_Normal;
  int team_id = -1;
  football::math::Vector3 stadium_position;
  if (facts.goal_scored) {
    kind = e_GameMode_KickOff;
    stadium_position = football::math::Vector3(0, 0, 0);
    team_id = 1 - facts.last_goal_team;
  } else if ((ballPos.coords[0] > 0 && lastSide > 0) ||
             (ballPos.coords[0] < 0 && lastSide < 0)) {
    kind = e_GameMode_Corner;
    float y = ballPos.coords[1];
    if (y > 0) y = facts.pitch.half_width(); else
               y = -facts.pitch.half_width();
    stadium_position = football::math::Vector3(facts.pitch.half_length() * lastSide, y, 0);
    team_id = 1 - lastTouchTeam;
  } else {
    // Attacker last touched over the opponent's goal line: do not reuse the
    // previous restart kind/position. Goal-area dimensions are legacy metres.
    kind = e_GameMode_GoalKick;
    team_id = 1 - lastTouchTeam;
    stadium_position = football::math::Vector3(
        (facts.pitch.half_length() - 5.5f) * -lastSide, 0, 0);
  }
  SubmitRestart(kind, static_cast<football::sim::TeamSide>(team_id),
                facts.stadium_to_home.Position(stadium_position), TickSpan{}, rulings);
}

void Referee::SettleTouchline(const football::math::Vector3& ballPos,
                              const RefereeTickFacts& facts, bool post_restart_grace,
                              football::sim::event::RulingSink& rulings) {
  if (post_restart_grace) return;
  foul.advantage = false;
  if (CheckFoul(facts.now, facts.pitch, facts.stadium_to_home,
                LiveFoulFacts(facts.home, facts.away), rulings)) return;
  rulings.Submit(football::sim::event::StopPlayRuling{});
  int lastTouchTeam = facts.last_touch_team;
  if (lastTouchTeam < 0) lastTouchTeam = facts.home.team_id;
  const int team_id = 1 - lastTouchTeam;
  football::math::Vector3 stadium_position;
  stadium_position.coords[0] = clamp(ballPos.coords[0],
      -facts.pitch.half_length() + 0.6f, facts.pitch.half_length() - 0.6f);
  if (ballPos.coords[1] >  0) stadium_position.coords[1] = facts.pitch.half_width();
  if (ballPos.coords[1] <= 0) stadium_position.coords[1] = -facts.pitch.half_width();
  stadium_position.coords[2] = 0;
  SubmitRestart(e_GameMode_ThrowIn, static_cast<football::sim::TeamSide>(team_id),
                facts.stadium_to_home.Position(stadium_position), TickSpan{}, rulings);
}

void Referee::GoalMouthCrossed(int side, const RefereeTickFacts& facts,
                               football::sim::event::RulingSink* rulings) {
  // The opponent of the side whose goal mouth was crossed scores.
  std::optional<football::sim::TeamSide> scorer_side;
  if (facts.home.execution_direction == -side) scorer_side = facts.home.side;
  else if (facts.away.execution_direction == -side) scorer_side = facts.away.side;
  if (scorer_side.has_value() && rulings != nullptr) {
    rulings->Submit(football::sim::event::AwardGoalRuling{*scorer_side, std::nullopt});
  }
}

void Referee::CheckPendingFoul(const football::sim::rules::RefereeView& view,
                               football::sim::event::RulingSink& rulings) {
  CheckFoul(view.tick.now, view.tick.pitch, view.tick.stadium_to_home,
            LiveFoulFacts(view.tick.home, view.tick.away), rulings);
}

void Referee::ConsumeBallTouch(const football::sim::rules::BallTouchFacts& facts,
                               football::sim::event::RulingSink& rulings) {
  EvaluateBallTouch(facts, rulings);
}

void Referee::EvaluateBallTouch(const football::sim::rules::BallTouchFacts& facts,
                                football::sim::event::RulingSink& rulings) {
  const football::sim::rules::RestartRuleFacts& restart = facts.restart;
  if (facts.release.eligible) {
    // A release requires an actual accepted scheduled touch, not cursor expiry
    // or the throw-in retain anchor's repeated non-kicked contact notices.
    const bool kick = facts.release.action_type == e_FunctionType_ShortPass ||
        facts.release.action_type == e_FunctionType_LongPass ||
        facts.release.action_type == e_FunctionType_HighPass ||
        facts.release.action_type == e_FunctionType_Shot ||
        (facts.release.action_type == e_FunctionType_BallControl &&
         facts.release.kind != e_GameMode_ThrowIn &&
         facts.release.intentional_kick);
    if ((facts.release.scheduled_contact || facts.release.physical_kick) && kick &&
        facts.release.ball_speed > 0.1f) {
      // The rules confirm ball-in-play; Runtime owns the state transition and
      // the clock start.
      rulings.Submit(football::sim::event::BallInPlayRuling{});
    }
  }

  if (!facts.offsides_enabled) {
    return;
  }
  // check for offside player receiving the ball

  const int lastTouchTeamID = facts.touch_team_id;
  if (lastTouchTeamID == -1) return; // shouldn't happen really ;)
  const int defending_team_id = 1 - lastTouchTeamID;
  const football::player::PlayerId ball_owner_id = facts.touch_player;
  const auto active_count = [&](int team_id) {
    return static_cast<int>(std::count_if(facts.all_active_players.begin(),
        facts.all_active_players.end(),
        [team_id](const football::sim::rules::RefereePlayerFact& player) {
          return player.team_id == team_id;
        }));
  };
  if (facts.in_play && !facts.in_set_piece &&
      (!restart.active ||
       (restart.active && restart.kind != e_GameMode_ThrowIn &&
        restart.kind != e_GameMode_Corner)) &&
      active_count(defending_team_id) > 1) {
      // disable if only 1 player: that's debug mode with only
                    // keeper
    for (const OffsideReference& reference : offsidePlayers) {
      if (reference.player == ball_owner_id) {
        foul.advantage = false;
        if (!CheckFoul(facts.now, *facts.pitch, facts.stadium_to_home,
                       LiveFoulFacts(facts.touch_team, facts.defending_team), rulings)) {
          // Offside takes effect at the enclosing synchronous touch boundary.
          rulings.Submit(football::sim::event::StopPlayRuling{});
          SubmitRestart(e_GameMode_FreeKick, static_cast<football::sim::TeamSide>(defending_team_id),
                        facts.stadium_to_home.Position(facts.touch_position), TickSpan{}, rulings,
                        football::sim::event::RestartCause{
                            football::sim::event::RestartCauseKind::Offside,
                            ball_owner_id, football::player::kInvalidPlayerId});
          break; // Setup clears offside state on the next rule tick, not during this iteration.
        }
      }
    }
  }

  // A reviewed opponent deflection does not erase the earlier attacking
  // team's offside positions. Touching by the flagged attacker is still judged
  // above; a teammate's touch still creates a new offside reference.
  if (facts.preserve_opponent_offside &&
      std::any_of(offsidePlayers.begin(), offsidePlayers.end(),
                  [&](const OffsideReference& reference) {
                    return reference.team_id != lastTouchTeamID;
                  })) return;

  offsidePlayers.clear();

  if (facts.in_play &&
      (!restart.active ||
       (restart.active && restart.kind != e_GameMode_ThrowIn &&
        restart.kind != e_GameMode_Corner))) {
    // check for offside players at moment of touch
    const float offside = football::sim::rules::GetOffsideLine(
        facts.all_active_players, facts.ball.position, *facts.pitch,
        defending_team_id, facts.defending_team.execution_direction);
    const signed int side = facts.touch_team.execution_direction;
    for (const football::sim::rules::RefereePlayerFact& player : facts.all_active_players) {
      if (player.team_id != lastTouchTeamID) continue;
      if (player.id != ball_owner_id) {
        if (player.position.coords[0] * side < offside * side) {
          offsidePlayers.push_back({player.id, player.team_id});
        }
      }
    }
  }
}

void Referee::AssessFoul(const FoulAssessment& fact, Tick now, bool restart_active) {
  if (restart_active) return;

  if (fact.kind == FoulKind::StandingFall) {
    // standing tackle
    if (fact.victim_team_fading_possession > 1.1 &&
        (fact.offender_action_type == static_cast<int>(e_FunctionType_Interfere) ||
         fact.offender_action_type == static_cast<int>(e_FunctionType_Sliding)) &&
        (fact.victim_position - fact.ball_position.Get2D()).GetLength() < 2.0 &&
        fact.offender_team_id != fact.victim_team_id) {
      // uooooga uooooga foul!
      foul.foulType = 1;
      foul.advantage = true;
      foul.offender = fact.offender;
      foul.victim = fact.victim;
      foul.offender_team_id = fact.offender_team_id;
      foul.victim_team_id = fact.victim_team_id;
      foul.foul_tick = now;
      foul.foulPosition = fact.position;
      foul.hasBeenProcessed = false;
    }
  } else if (fact.kind == FoulKind::SlidingTackle &&
             (fact.offender != foul.offender || foul.foulType == 0)) {
    // sliding tackle
    if (now - fact.offender_last_touch_tick > kTouchGrace &&
        fact.offender_action_type == static_cast<int>(e_FunctionType_Sliding) &&
        fact.offender_team_id != fact.victim_team_id &&
        (fact.ball_position - fact.victim_position).GetLength() < 8.0) {
      const float severity = fact.severity;
      if (severity > 1.0) {
        // uooooga uooooga foul!
        foul.foulType = 1;
        foul.advantage = false;
        foul.offender = fact.offender;
        foul.victim = fact.victim;
        foul.offender_team_id = fact.offender_team_id;
        foul.victim_team_id = fact.victim_team_id;
        foul.foul_tick = now;
        foul.foulPosition = fact.position;
        foul.hasBeenProcessed = false;
        if (severity > 1.4) foul.foulType = 2;
        if (severity > 2.0) foul.foulType = 3;
      }
    }
  }
}

bool Referee::CheckFoul(Tick now, const football::pitch::Pitch& pitch,
                        PitchFrameTransform stadium_to_home, const FoulLiveFacts& live,
                        football::sim::event::RulingSink& rulings) {
  bool penalty = false;
  if (foul.foulType != 0) {
    if (fabs(foul.foulPosition.coords[1]) < 20.15f - pitch.line_half_width() &&
        foul.foulPosition.coords[0] *
                -live.For(foul.victim_team_id).defending_direction >
            pitch.half_length() - pitch.penalty_area_depth() + pitch.line_half_width())
      penalty = true;
  }

  if (foul.advantage) {
    if (penalty) {
      foul.advantage = false;
    } else {
      if (now > foul.foul_tick + kAdvantageRecheck) {
        if (now > foul.foul_tick + kAdvantageExpiry) {
          // cancel foul, advantage took long enough
          foul.offender = football::player::kInvalidPlayerId;
          foul.victim = football::player::kInvalidPlayerId;
          foul.foulType = 0;
        } else {
          // calculate if there's advantage still
          if (live.For(foul.victim_team_id).fading_possession_amount < 1.0) {
            foul.advantage = false;
          }
        }
      }
    }
  }

  if (foul.foulType != 0 && foul.advantage == false && !foul.hasBeenProcessed) {
    rulings.Submit(football::sim::event::StopPlayRuling{});
    const football::math::Vector3 stadium_position = penalty
        ? football::math::Vector3(
              (pitch.half_length() - pitch.penalty_mark_distance()) *
                  live.For(foul.offender_team_id).defending_direction,
              0, 0)
        : foul.foulPosition;
    const football::sim::TeamSide team_side =
        static_cast<football::sim::TeamSide>(foul.victim_team_id);
    SubmitRestart(penalty ? e_GameMode_Penalty : e_GameMode_FreeKick, team_side,
                  stadium_to_home.Position(stadium_position),
                  foul.foulType >= 2 ? kCardAdministration : TickSpan{}, rulings,
                  football::sim::event::RestartCause{
                      football::sim::event::RestartCauseKind::Foul,
                      foul.offender, foul.victim});
    if (foul.foulType == 2 || foul.foulType == 3) {
      rulings.Submit(football::sim::event::CardRuling{
          foul.offender, foul.foulType, now + kCardEffectDelay});
    }

    foul.hasBeenProcessed = true;

    return true;
  }

  return false;
}
