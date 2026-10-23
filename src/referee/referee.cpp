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
#include "referee/ball_rules.hpp"
#include "referee/foul_sanction.hpp"

namespace {
using football::sim::Tick;
using football::sim::BallOutOfPlay;
using football::sim::ClassifyBallOutOfPlay;
using football::sim::FoulAssessment;
using football::sim::FoulKind;
using football::sim::ChallengeAction;
using football::sim::TickSpan;
using football::sim::Seconds;
using football::sim::rules::RefereeTickFacts;
using football::sim::rules::RefereeTeamFact;
using football::sim::rules::RestartReleaseAction;
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

// Law 11: a player cannot be offside directly from a goal kick, a corner
// kick or a throw-in. The restart kind is the only exemption the rules need.
bool OffsideExemptRestart(e_GameMode kind) {
  return kind == e_GameMode_GoalKick || kind == e_GameMode_Corner ||
         kind == e_GameMode_ThrowIn;
}

}  // namespace

Referee::Referee() {
  foul.offender = football::player::kInvalidPlayerId;
  foul.victim = football::player::kInvalidPlayerId;
  foul.offender_team_id = -1;
  foul.victim_team_id = -1;
  foul.foulType = 0;
  foul.advantage = false;
  foul.advantage_kept = false;
  foul.dogso = false;
  foul.attempted_to_play_ball = false;
  foul.foul_tick = {};
  foul.hasBeenProcessed = true;
}

Referee::~Referee() {}

void Referee::OnCeremonialSetup() {
  // A placed/repositioned setup invalidates the last-touch offside position.
  offside_.Clear();
}

void Referee::OnRestartTaken() {
  // The awarded restart completed; abandon the foul that produced it.
  foul.offender = football::player::kInvalidPlayerId;
  foul.victim = football::player::kInvalidPlayerId;
  foul.foulType = 0;
  foul.dogso = false;
  foul.attempted_to_play_ball = false;
}

void Referee::OnPeriodEnded(MatchPhase ended_phase) {
  // Full time keeps the terminal rule state; play/restart lifecycle belongs to
  // Runtime. A half-time break abandons a pending foul.
  if (ended_phase == MatchPhase::SecondHalf) return;
  foul.offender = football::player::kInvalidPlayerId;
  foul.victim = football::player::kInvalidPlayerId;
  foul.foulType = 0;
  foul.advantage = false;
  foul.advantage_kept = false;
  foul.dogso = false;
  foul.attempted_to_play_ball = false;
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
  // The first boundary the whole ball actually crossed owns the restart, not
  // the tick-end centre: a diagonal exit can be past both lines at once.
  const football::sim::BallBoundaryCrossing crossing =
      football::sim::DetectBallBoundaryCrossing(
          tick.pitch, tick.ball.previous_position, tick.ball.position,
          tick.ball.radius);
  const football::math::Vector3& ballPos = crossing.position;
  if (crossing.boundary == BallOutOfPlay::GoalLine || tick.goal_scored) {
    SettleGoalLine(ballPos, tick, rulings);
  } else if (crossing.boundary == BallOutOfPlay::Touchline) {
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
    isFoul = CheckFoul(facts.now, facts.pitch,
                       LiveFoulFacts(facts.home, facts.away), rulings);
  } else {
    foul.foulType = 0;
  }
  if (isFoul) return;
  FlushPendingDiscipline(facts.now, rulings);
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
                stadium_position, TickSpan{}, rulings);
}

void Referee::SettleTouchline(const football::math::Vector3& ballPos,
                              const RefereeTickFacts& facts, bool post_restart_grace,
                              football::sim::event::RulingSink& rulings) {
  if (post_restart_grace) return;
  foul.advantage = false;
  if (CheckFoul(facts.now, facts.pitch,
                LiveFoulFacts(facts.home, facts.away), rulings)) return;
  FlushPendingDiscipline(facts.now, rulings);
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
                stadium_position, TickSpan{}, rulings);
}

void Referee::GoalMouthCrossed(int side, const RefereeTickFacts& facts,
                               football::sim::event::RulingSink* rulings) {
  // The opponent of the side whose goal mouth was crossed scores.
  std::optional<football::sim::TeamSide> scorer_side;
  if (facts.home.defending_direction == -side) scorer_side = facts.home.side;
  else if (facts.away.defending_direction == -side) scorer_side = facts.away.side;
  if (scorer_side.has_value() && rulings != nullptr) {
    rulings->Submit(football::sim::event::AwardGoalRuling{*scorer_side, std::nullopt});
  }
}

void Referee::CheckPendingFoul(const football::sim::rules::RefereeView& view,
                               football::sim::event::RulingSink& rulings) {
  CheckFoul(view.tick.now, view.tick.pitch,
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
    const bool kick = facts.release.action == RestartReleaseAction::ShortPass ||
        facts.release.action == RestartReleaseAction::LongPass ||
        facts.release.action == RestartReleaseAction::HighPass ||
        facts.release.action == RestartReleaseAction::Shot ||
        (facts.release.action == RestartReleaseAction::BallControl &&
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
  const bool restart_exempt = restart.active && OffsideExemptRestart(restart.kind);
  // An offence can only be called in open play; a set-piece administration is
  // not yet open play. A snapshot, however, must also be captured on the
  // release touch of a non-exempt restart (a free kick).
  const bool offence_evaluable =
      facts.in_play && !facts.in_set_piece && !restart_exempt;
  const bool snapshot_allowed =
      !restart_exempt &&
      ((facts.in_play && !facts.in_set_piece) ||
       (facts.in_set_piece && facts.release.eligible));

  // 1. Offside offence: an attacker flagged at the last attacking touch plays
  //    the ball. This holds across a deflection or a deliberate save (Law 11).
  if (offence_evaluable && active_count(defending_team_id) > 1) {
    for (const OffsideReference& reference : offside_.candidates) {
      if (reference.player != ball_owner_id) continue;
      foul.advantage = false;
      if (!CheckFoul(facts.now, *facts.pitch,
                     LiveFoulFacts(facts.touch_team, facts.defending_team), rulings)) {
        // Offside takes effect at the enclosing synchronous touch boundary.
        rulings.Submit(football::sim::event::StopPlayRuling{});
        SubmitRestart(e_GameMode_FreeKick,
                      static_cast<football::sim::TeamSide>(defending_team_id),
                      facts.touch_position, TickSpan{}, rulings,
                      football::sim::event::RestartCause{
                          football::sim::event::RestartCauseKind::Offside,
                          ball_owner_id, football::player::kInvalidPlayerId});
      }
      break;  // Setup clears the snapshot on the next rule tick.
    }
  }

  // 2. Update the offside position for the next touch.
  if (!snapshot_allowed) {
    offside_.Clear();
    return;
  }
  if (!offside_.Empty() && offside_.attacking_team_id != lastTouchTeamID) {
    // An opponent touched the ball. A deliberate play by the defender resets
    // the offside position; a deflection or a deliberate save leaves it
    // standing, so a later flagged attacker is still penalised.
    if (facts.play_kind == football::sim::rules::BallPlayKind::DeliberatePlay)
      offside_.Clear();
    return;
  }

  // An attacking teammate (or the first touch of a phase) played the ball:
  // capture the offside position at this instant.
  const float offside = football::sim::rules::GetOffsideLine(
      facts.all_active_players, facts.ball.position, *facts.pitch,
      defending_team_id, facts.defending_team.defending_direction);
  const signed int side = facts.touch_team.defending_direction;
  offside_.created_at = facts.now;
  offside_.passer = ball_owner_id;
  offside_.attacking_team_id = lastTouchTeamID;
  offside_.candidates.clear();
  for (const football::sim::rules::RefereePlayerFact& player : facts.all_active_players) {
    if (player.team_id != lastTouchTeamID) continue;
    if (player.id == ball_owner_id) continue;
    if (player.position.coords[0] * side < offside * side)
      offside_.candidates.push_back({player.id, player.team_id});
  }
}

void Referee::AssessFoul(const FoulAssessment& fact, Tick now, bool restart_active) {
  if (restart_active) return;
  if (fact.offender_team_id == fact.victim_team_id) return;

  // The contact solver only reports a suspicion for a real challenge. The
  // referee confirms the foul and classifies the sanction from frozen facts.
  if (fact.kind == FoulKind::StandingFall) {
    // standing tackle
    if (!(fact.victim_team_fading_possession > 1.1f)) return;
    if (fact.action == ChallengeAction::Other) return;
    if ((fact.victim_position - fact.ball_position.Get2D()).GetLength() >= 2.0f)
      return;
  } else {
    // sliding tackle
    if (now - fact.offender_last_touch_tick <= kTouchGrace) return;
    if (fact.action != ChallengeAction::SlidingChallenge) return;
    if ((fact.ball_position - fact.victim_position).GetLength() >= 8.0f) return;
    if (fact.offender == foul.offender && foul.foulType != 0) return;
  }

  const football::sim::rules::FoulSanction sanction =
      football::sim::rules::ClassifyFoul(fact.kind, fact.severity,
                                         fact.denied_goal_opportunity);
  if (!sanction.foul) return;

  // Law 12 double jeopardy is applied later, where the offender's penalty area
  // is known; here the raw card reflects severity and DOGSO.
  foul.foulType = sanction.card;
  foul.advantage = fact.kind == FoulKind::StandingFall;
  foul.advantage_kept = false;
  foul.dogso = sanction.dogso;
  foul.severity = sanction.severity;
  foul.attempted_to_play_ball = fact.attempted_to_play_ball;
  foul.offender = fact.offender;
  foul.victim = fact.victim;
  foul.offender_team_id = fact.offender_team_id;
  foul.victim_team_id = fact.victim_team_id;
  foul.foul_tick = now;
  foul.foulPosition = fact.position;
  foul.hasBeenProcessed = false;
}

bool Referee::CheckFoul(Tick now, const football::pitch::Pitch& pitch,
                        const FoulLiveFacts& live,
                        football::sim::event::RulingSink& rulings) {
  bool penalty = false;
  if (foul.foulType != 0) {
    if (fabs(foul.foulPosition.coords[1]) < 20.15f - pitch.line_half_width() &&
        foul.foulPosition.coords[0] *
                -live.For(foul.victim_team_id).defending_direction >
            pitch.half_length() - pitch.penalty_area_depth() + pitch.line_half_width())
      penalty = true;
  }
  // Law 12 double jeopardy: a penalty plus a genuine attempt to play the ball
  // is a caution, not a send-off. It never downgrades a send-off that stands on
  // its own via excessive force, only one whose sole reason is the DOGSO.
  if (penalty && foul.dogso && foul.attempted_to_play_ball &&
      foul.severity != football::sim::rules::FoulSeverity::Excessive &&
      foul.foulType == 3)
    foul.foulType = 2;

  if (foul.advantage) {
    if (penalty) {
      // A penalty cannot play on.
      foul.advantage = false;
    } else if (now > foul.foul_tick + kAdvantageRecheck) {
      if (now > foul.foul_tick + kAdvantageExpiry) {
        // The advantage played out: the restart is cancelled, but a
        // card-worthy foul is still disciplined at this stoppage.
        foul.advantage = false;
        foul.advantage_kept = true;
      } else if (live.For(foul.victim_team_id).fading_possession_amount < 1.0f) {
        // Possession did not recover: recall the foul.
        foul.advantage = false;
      }
    }
  }

  if (foul.foulType != 0 && !foul.advantage && !foul.hasBeenProcessed) {
    if (foul.advantage_kept) {
      // The realized advantage cancels the restart, not the discipline: hold
      // the card until the next stoppage, which Runtime signals by settling a
      // stop. This path emits no stop of its own.
      if (foul.foulType >= 2) {
        foul.pending_card = foul.foulType;
        foul.pending_offender = foul.offender;
      }
      foul.foulType = 0;
      foul.advantage_kept = false;
      foul.hasBeenProcessed = true;
      return false;
    }
    FlushPendingDiscipline(now, rulings);
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
                  stadium_position,
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

void Referee::FlushPendingDiscipline(Tick now,
                                     football::sim::event::RulingSink& rulings) {
  if (foul.pending_card < 2) return;
  rulings.Submit(football::sim::event::CardRuling{
      foul.pending_offender, foul.pending_card, now + kCardEffectDelay});
  foul.pending_card = 0;
  foul.pending_offender = football::player::kInvalidPlayerId;
}
