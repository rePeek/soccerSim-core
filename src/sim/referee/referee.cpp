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

#include "sim/referee/referee.hpp"
#include <cmath>
#include <type_traits>
#include <algorithm>

#include "football/ball/ball.hpp"
#include "sim/player/player.hpp"
#include "sim/team/team.hpp"
#include "sim/referee/offside.hpp"
#include "sim/referee/ball_rules.hpp"
#include "sim/observation/mentalimage.hpp"
#include "sim/referee/restart_placement.hpp"
#include "sim/observation/pitch_frame.hpp"

namespace {
using football::sim::Tick;
using football::sim::BallOutOfPlay;
using football::sim::ClassifyBallOutOfPlay;
using football::sim::FoulAssessment;
using football::sim::FoulKind;
using football::sim::TickSpan;
using football::sim::Seconds;
using football::sim::rules::RefereeTickFacts;
using football::sim::rules::RuleCommandSink;
// Opening/half-time ceremonies retain their old schedule in this stage.
constexpr auto kCardAdministration = Seconds(10);
constexpr auto kRestartWhistle = Seconds(2);
// Initial engineering bounds, not calibrated match-duration distributions.
RestartPolicy PolicyFor(e_GameMode mode) {
  switch (mode) {
    case e_GameMode_FreeKick: return {TickSpan{}, Seconds(30)}; // Quick restart allowed.
    case e_GameMode_ThrowIn: return {Seconds(2), Seconds(20)};
    case e_GameMode_GoalKick: return {Seconds(3), Seconds(30)};
    case e_GameMode_Corner: return {Seconds(5), Seconds(30)};
    case e_GameMode_Penalty: return {Seconds(2), Seconds(30)};
    case e_GameMode_KickOff: return {Seconds(5), Seconds(30)};
    default: throw std::logic_error("unsupported restart policy");
  }
}
constexpr TickSpan kHalfPreparation{10};
constexpr TickSpan kHalfWhistle{20};
constexpr TickSpan kPostRestartRelax{40};
constexpr TickSpan kTouchGrace{60};
constexpr TickSpan kAdvantageRecheck{60};
constexpr auto kAdvantageExpiry = Seconds(3);
constexpr auto kCardEffectDelay = Seconds(6);

// Compatibility evidence capture for the direct TripNotice test adapter.
// Production captures the same fields in player/player_contact.cpp.
FoulAssessment LiveFoulAssessment(Player* tripee, Player* tripper, int tackleType,
                                  const Vector3& ball_position, Tick now) {
  const PlayerActionState& action = tripper->GetSimulationActionState();
  FoulAssessment fact;
  fact.kind = tackleType == 3 ? FoulKind::SlidingTackle : FoulKind::StandingFall;
  fact.victim = tripee->GetID();
  fact.offender = tripper->GetID();
  fact.victim_team_id = tripee->GetTeam()->GetID();
  fact.offender_team_id = tripper->GetTeam()->GetID();
  fact.contacted_at = now;
  fact.position = tripee->GetPitchPosition();
  fact.victim_position = tripee->GetPosition();
  fact.victim_direction = tripee->GetDirectionVec();
  fact.offender_position = tripper->GetPosition();
  fact.ball_position = ball_position;
  fact.offender_action_type = static_cast<int>(action.type);
  fact.offender_scheduled_contact = action.HasScheduledContact();
  fact.offender_contact_frame = action.ContactFrame();
  fact.offender_frame = action.Frame();
  fact.offender_contact_position = action.contactPosition;
  fact.offender_last_touch_tick = tripper->GetLastTouchTick();
  fact.victim_team_fading_possession =
      tripee->GetTeam()->GetFadingTeamPossessionAmount();
  // Sliding severity is normally frozen by the solver; reproduce it for the
  // test adapter only.
  float severity = 1.0;
  if (fact.offender_scheduled_contact) {
    severity = std::pow(clamp(fabs(fact.offender_contact_frame - fact.offender_frame) /
                                  fact.offender_contact_frame,
                              0.0, 1.0),
                    0.7) * 0.5;
    severity += NormalizedClamp(
        (fact.ball_position - fact.offender_contact_position).GetLength(), 0.0, 2.0) * 0.5;
  }
  severity += (fact.victim_position - fact.offender_position)
                  .GetNormalized(0)
                  .GetDotProduct(fact.victim_direction) * 0.5 + 0.5;
  fact.severity = severity;
  fact.score = NormalizedClamp(severity, 1.0f, 2.0f);
  return fact;
}
}  // namespace

Referee::Referee(Team& kickoff_team, const Vector3& kickoff_position) {
  buffer.desiredSetPiece = e_GameMode_KickOff;
  buffer.teamID = kickoff_team.GetID();
  buffer.setpiece_team = &kickoff_team;
  buffer.stop_tick = {};
  buffer.prepare_tick = {};
  buffer.start_tick = Tick{} + kRestartWhistle;
  buffer.restartPos = kickoff_position;
  buffer.taker = 0;
  buffer.endPhase = true;
  buffer.active = true;

  foul.foulPlayer = 0;
  foul.foulType = 0;
  foul.advantage = false;
  foul.foul_tick = {};
  foul.hasBeenProcessed = true;

  post_restart_relax_ = {};
}

Referee::~Referee() {}

void Referee::OnPeriodEnded(MatchPhase ended_phase, Tick now,
                            const Vector3& kickoff_position, Team& kickoff_team) {
  buffer.active = false;
  buffer.taker = nullptr;
  buffer.endPhase = false;
  buffer.restart.reset();
  if (ended_phase == MatchPhase::SecondHalf) return;

  // Half time: abandon the pending foul and prepare the next kickoff.
  foul.foulPlayer = nullptr;
  foul.foulType = 0;
  foul.advantage = false;
  foul.foul_tick = {};
  foul.hasBeenProcessed = true;
  buffer.desiredSetPiece = e_GameMode_KickOff;
  buffer.stop_tick = now;
  buffer.prepare_tick = buffer.stop_tick + kHalfPreparation;
  buffer.start_tick = buffer.prepare_tick + kHalfWhistle;
  buffer.restartPos = kickoff_position;
  buffer.active = true;
  buffer.endPhase = true;
  buffer.teamID = kickoff_team.GetID();
  buffer.setpiece_team = &kickoff_team;
}

void Referee::Advance(const football::sim::rules::RefereeView& view,
                      const MatchOptions& options, blunted::Rng& rng,
                      RuleCommandSink& commands) {
  const RefereeTickFacts& facts = view.tick;
  if (facts.phase == MatchPhase::Finished) return;
  if (buffer.active && buffer.restart) {
    ProcessRestart(facts, options, rng, commands);
    if (post_restart_relax_ > TickSpan{}) post_restart_relax_ = post_restart_relax_ - TickSpan{1};
    return;
  }
  if (!(facts.play_authorized && !facts.set_piece_active)) {
    // not in play, maybe something needs to happen?
    if (!facts.play_authorized && !facts.set_piece_active && buffer.active == true) {
      if (buffer.taker == nullptr && facts.now >= buffer.prepare_tick) {
        if (buffer.endPhase == true) {
          if (facts.phase == MatchPhase::PreMatch) {
            commands.SetPhase(MatchPhase::FirstHalf);
          }
          buffer.endPhase = false;
        }
        // Deterministic reseed before positioning players for every restart.
        rng.Seed(options.game_engine_random_seed);
        PrepareCeremonialKickOff(facts, options, rng, commands);
      }
      if (buffer.taker != nullptr && facts.now >= buffer.start_tick) {
        // blow whistle and wait for set piece taker to touch the ball
        commands.StartPlay();
        commands.StartSetPiece();
        // Keep ceremonial schedule/placement, but never fabricate a kickoff.
        // Authorization and accepted contact share the ordinary release states.
        RestartState ready;
        ready.entered_tick = buffer.stop_tick;
        ready.earliest_restart_tick = buffer.start_tick;
        ready.timeout_tick = buffer.start_tick;
        ready.phase = RestartPhase::Ready;
        ready.setup_done = true;
        buffer.restart = std::move(ready);
      }
    }
  }
  if (post_restart_relax_ > TickSpan{})
    post_restart_relax_ = post_restart_relax_ - TickSpan{1};
}


void Referee::EvaluateOutOfPlay(const football::sim::rules::RefereeView& view,
                                RuleCommandSink& commands) {
  const RefereeTickFacts& tick = view.tick;
  if (tick.phase == MatchPhase::Finished) return;
  if (!(tick.play_authorized && !tick.set_piece_active)) return;
  const auto home_ball = tick.ball_to_home.Position(tick.ball.Predict(TickSpan{}));
  // Legacy foul/side data uses stadium coordinates, including switched ends.
  const Vector3 ballPos = tick.stadium_to_home.Position(home_ball);
  // Goal-line priority: the legacy local authorization was cleared when the goal
  // line applied, so the sideline branch could not run at the same instant.
  const BallOutOfPlay out = ClassifyBallOutOfPlay(tick.pitch, ballPos);
  if (out == BallOutOfPlay::GoalLine || tick.goal_scored) {
    SettleGoalLine(ballPos, tick, commands);
  } else if (out == BallOutOfPlay::Touchline) {
    SettleTouchline(ballPos, tick, commands);
  }
}

void Referee::SettleGoalLine(const Vector3& ballPos, const RefereeTickFacts& facts,
                             RuleCommandSink& commands) {
  const auto team = [&](int id) { return id == 0 ? &facts.home : &facts.away; };
  foul.advantage = false;
  bool isFoul = false;
  if (!facts.goal_scored) {
    isFoul = CheckFoul(facts.now, facts.pitch, facts.stadium_to_home, commands);
  } else {
    foul.foulType = 0;
  }
  if (isFoul) return;
  commands.StopPlay();
  // corner, goal kick or kick off?
  Team* lastTouchTeam = facts.last_touch_team;
  if (lastTouchTeam == 0) lastTouchTeam = team(facts.first_team);
  signed int lastSide = lastTouchTeam->GetStaticSide();
  if (facts.goal_scored) {
    buffer.desiredSetPiece = e_GameMode_KickOff;
    buffer.restartPos = Vector3(0, 0, 0);
    buffer.teamID = facts.last_goal_team->Opponent()->GetID();
  } else if ((ballPos.coords[0] > 0 && lastSide > 0) ||
             (ballPos.coords[0] < 0 && lastSide < 0)) {
    buffer.desiredSetPiece = e_GameMode_Corner;
    float y = ballPos.coords[1];
    if (y > 0) y = facts.pitch.half_width(); else
               y = -facts.pitch.half_width();
    buffer.restartPos = Vector3(facts.pitch.half_length() * lastSide, y, 0);
    buffer.teamID = 1 - lastTouchTeam->GetID();
  } else {
    buffer.desiredSetPiece = e_GameMode_GoalKick;
    buffer.restartPos = Vector3(facts.pitch.half_length() * 0.92 * -lastSide, 0, 0);
    buffer.teamID = 1 - lastTouchTeam->GetID();
  }
  ScheduleRestart({facts.now, team(buffer.teamID), facts.stadium_to_home});
}

void Referee::SettleTouchline(const Vector3& ballPos, const RefereeTickFacts& facts,
                              RuleCommandSink& commands) {
  if (post_restart_relax_ != TickSpan{}) return;
  const auto team = [&](int id) { return id == 0 ? &facts.home : &facts.away; };
  foul.advantage = false;
  if (CheckFoul(facts.now, facts.pitch, facts.stadium_to_home, commands)) return;
  commands.StopPlay();
  Team* lastTouchTeam = facts.last_touch_team;
  if (lastTouchTeam == 0) lastTouchTeam = &facts.home;
  buffer.teamID = 1 - lastTouchTeam->GetID();
  buffer.desiredSetPiece = e_GameMode_ThrowIn;
  buffer.restartPos.coords[0] = clamp(ballPos.coords[0], -facts.pitch.half_length() + 0.6f, facts.pitch.half_length() - 0.6f);
  if (ballPos.coords[1] >  0) buffer.restartPos.coords[1] = facts.pitch.half_width();
  if (ballPos.coords[1] <= 0) buffer.restartPos.coords[1] = -facts.pitch.half_width();
  buffer.restartPos.coords[2] = 0;
  ScheduleRestart({facts.now, team(buffer.teamID), facts.stadium_to_home});
}

void Referee::GoalMouthCrossed(int side, const RefereeTickFacts& facts,
                               football::sim::event::RulingSink* rulings) {
  // The opponent of the side whose goal mouth was crossed scores.
  Team* scorer_team = nullptr;
  if (facts.home.GetDynamicSide() == -side) scorer_team = &facts.home;
  else if (facts.away.GetDynamicSide() == -side) scorer_team = &facts.away;
  if (scorer_team != nullptr && rulings != nullptr) {
    rulings->Submit(football::sim::event::AwardGoalRuling{
        scorer_team->GetTeamSide(), std::nullopt});
  }
}

void Referee::CheckPendingFoul(const football::sim::rules::RefereeView& view,
                               RuleCommandSink& commands) {
  CheckFoul(view.tick.now, view.tick.pitch, view.tick.stadium_to_home, commands);
}

void Referee::Consume(const football::sim::event::StampedFact& fact,
                      const football::sim::rules::RefereeView& view,
                      RuleCommandSink& commands) {
  const auto* touch = std::get_if<football::sim::event::BallTouchFact>(&fact.fact);
  if (touch != nullptr) ConsumeBallTouch(fact.tick, *touch, view, commands);
}


void Referee::ConsumeBallTouch(Tick now,
                               const football::sim::event::BallTouchFact& fact,
                               const football::sim::rules::RefereeView& view,
                               RuleCommandSink& commands) {
  Player* player = FindPlayer(view.tick, fact.player);
  if (player == nullptr) return;
  Team* team = player->GetTeam();
  football::sim::rules::BallTouchFacts facts;
  facts.now = now;
  facts.touch_player = player;
  facts.touch_team_id = team->GetID();
  facts.touch_team = team;
  facts.defending_team = team->Opponent();
  facts.in_play = view.tick.play_authorized;
  facts.in_set_piece = view.tick.set_piece_active;
  facts.offsides_enabled = view.offsides_enabled;
  facts.ball = &view.tick.ball;
  facts.pitch = &view.tick.pitch;
  facts.all_active_players = view.all_active_players;
  facts.stadium_to_home = view.tick.stadium_to_home;
  EvaluateBallTouch(facts, commands);
}



Player* Referee::FindPlayer(const RefereeTickFacts& facts,
                           football::model::PlayerId id) const {
  for (Player* player : facts.home.GetAllPlayers()) {
    if (player->GetID() == id) return player;
  }
  for (Player* player : facts.away.GetAllPlayers()) {
    if (player->GetID() == id) return player;
  }
  return nullptr;
}

void Referee::PrepareCeremonialKickOff(const RefereeTickFacts& facts,
                                     const MatchOptions& options, blunted::Rng& rng,
                                     RuleCommandSink& commands) {
  // Opening/half-time placement is intentionally separate from ordinary readiness.
  commands.ResetSituation(options.reverse_team_processing ? -buffer.restartPos : buffer.restartPos);
  Team* first = facts.first_team == 0 ? &facts.home : &facts.away;
  Team* second = facts.first_team == 0 ? &facts.away : &facts.home;
  Player *first_taker = PositionRestartPlayers(facts.pitch, first, e_GameMode_KickOff, second,
      buffer.setpiece_team->GetID(), buffer.teamID, facts.ball, facts.regulation, options, rng, commands);
  Player *second_taker = PositionRestartPlayers(facts.pitch, second, e_GameMode_KickOff, first,
      buffer.setpiece_team->GetID(), buffer.teamID, facts.ball, facts.regulation, options, rng, commands);
  buffer.taker = buffer.teamID == facts.first_team ? first_taker : second_taker;
  offsidePlayers.clear();
}

// Compatibility adapter for direct referee tests; production consumes facts.
void Referee::BallTouched(const football::sim::rules::BallTouchFacts& facts,
                          RuleCommandSink& commands) {
  EvaluateBallTouch(facts, commands);
}

void Referee::EvaluateBallTouch(const football::sim::rules::BallTouchFacts& facts,
                                RuleCommandSink& commands) {
  if (buffer.active && buffer.restart && buffer.restart->phase == RestartPhase::Ready &&
      facts.touch_player == buffer.taker) {
    const auto& action = buffer.taker->GetSimulationActionState();
    // A release requires an actual accepted scheduled touch, not cursor expiry
    // or the throw-in retain anchor's repeated non-kicked contact notices.
    const bool kick = action.type == e_FunctionType_ShortPass || action.type == e_FunctionType_LongPass ||
        action.type == e_FunctionType_HighPass || action.type == e_FunctionType_Shot ||
        (action.type == e_FunctionType_BallControl && buffer.desiredSetPiece != e_GameMode_ThrowIn &&
         buffer.taker->GetLastTouchType() == e_TouchType_Intentional_Kicked);
    if (action.contact && action.elapsed == *action.contact && kick &&
        facts.ball->GetMovement().GetLength() > 0.1f) {
      buffer.restart->phase = RestartPhase::Taken;
      commands.StartBallInPlay();
    }
  }

  if (!facts.offsides_enabled) {
    return;
  }
  // check for offside player receiving the ball

  const int lastTouchTeamID = facts.touch_team_id;
  if (lastTouchTeamID == -1) return; // shouldn't happen really ;)
  const int defending_team_id = 1 - lastTouchTeamID;
  const auto active_count = [&](int team_id) {
    return static_cast<int>(std::count_if(facts.all_active_players.begin(),
        facts.all_active_players.end(),
        [team_id](const Player* player) { return player->GetTeamID() == team_id; }));
  };
  if (facts.in_play && !facts.in_set_piece && buffer.active == false &&
      active_count(defending_team_id) > 1) {
      // disable if only 1 player: that's debug mode with only
                    // keeper
    Player* ballOwner = facts.touch_player;
    for (auto p : offsidePlayers) {
      if (p == ballOwner) {
        foul.advantage = false;
        if (!CheckFoul(facts.now, *facts.pitch, facts.stadium_to_home, commands)) {
          // uooooga uooooga offside!
          commands.StopPlay();
          buffer.desiredSetPiece = e_GameMode_FreeKick;
          buffer.restartPos = ballOwner->GetPitchPosition();
          buffer.teamID = defending_team_id;
          ScheduleRestart({facts.now, facts.defending_team, facts.stadium_to_home});
          break; // Setup clears offside state on the next rule tick, not during this iteration.
        }
      }
    }
  }

  offsidePlayers.clear();

  if (facts.in_play &&
      (buffer.active == false ||
       (buffer.active == true && buffer.desiredSetPiece != e_GameMode_ThrowIn &&
        buffer.desiredSetPiece != e_GameMode_Corner))) {
    // check for offside players at moment of touch
    MentalImage mentalImage(facts.now, facts.all_active_players, *facts.ball);
    float offside = football::sim::rules::GetOffsideLine(mentalImage, facts.now, *facts.pitch,
        *facts.ball, defending_team_id, facts.defending_team->GetDynamicSide());
    const signed int side = facts.touch_team->GetDynamicSide();
    for (Player* player : facts.all_active_players) {
      if (player->GetTeamID() != lastTouchTeamID) continue;
      if (player != facts.touch_player) {
        if (player->GetPosition().coords[0] * side < offside * side) {
          offsidePlayers.push_back(player);
        }
      }
    }
  }
}

void Referee::AssessFoul(Player* tripee, Player* tripper,
                         const FoulAssessment& fact, Tick now) {
  if (buffer.active) return;

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
      foul.foulPlayer = tripper;
      foul.foulVictim = tripee;
      foul.foul_tick = now;
      foul.foulPosition = fact.position;
      foul.hasBeenProcessed = false;
    }
  } else if (fact.kind == FoulKind::SlidingTackle &&
             (tripper != foul.foulPlayer || foul.foulType == 0)) {
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
        foul.foulPlayer = tripper;
        foul.foulVictim = tripee;
        foul.foul_tick = now;
        foul.foulPosition = fact.position;
        foul.hasBeenProcessed = false;
        if (severity > 1.4) foul.foulType = 2;
        if (severity > 2.0) foul.foulType = 3;
      }
    }
  }
}

// Compatibility adapter for direct referee tests; production consumes facts.
void Referee::TripNotice(Player *tripee, Player *tripper, int tackleType,
                         Tick now, const Vector3& ball_position) {
  if (buffer.active) return;  // Gate precedes actor reads, including null probes.
  // A little standing trip (type 1) was never a foul, in either implementation.
  if (tackleType != 2 && tackleType != 3) return;
  FoulAssessment fact;
  if (tripee != nullptr && tripper != nullptr) {
    fact = LiveFoulAssessment(tripee, tripper, tackleType, ball_position, now);
  } else {
    fact.kind = tackleType == 3 ? FoulKind::SlidingTackle : FoulKind::StandingFall;
    fact.contacted_at = now;
    fact.ball_position = ball_position;
  }
  AssessFoul(tripee, tripper, fact, now);
}


bool Referee::CheckFoul(Tick now, const football::model::Pitch& pitch,
                        PitchFrameTransform stadium_to_home,
                        RuleCommandSink& commands) {

  bool penalty = false;
  if (foul.foulType != 0) {
    if (fabs(foul.foulPosition.coords[1]) < 20.15f - pitch.line_half_width() &&
        foul.foulPosition.coords[0] *
                -foul.foulVictim->GetTeam()->GetStaticSide() >
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

          foul.foulPlayer = 0;
          foul.foulType = 0;
        } else {
          // calculate if there's advantage still
          if (foul.foulVictim->GetTeam()->GetFadingTeamPossessionAmount() <
              1.0) {
            foul.advantage = false;
          }
        }
      }
    }
  }

  if (foul.foulType != 0 && foul.advantage == false && !foul.hasBeenProcessed) {

    commands.StopPlay();
    buffer.desiredSetPiece = penalty ? e_GameMode_Penalty : e_GameMode_FreeKick;
    buffer.restartPos = penalty
        ? Vector3((pitch.half_length() - pitch.penalty_mark_distance()) * foul.foulPlayer->GetTeam()->GetStaticSide(),
                  0, 0)
        : foul.foulPosition;
    buffer.teamID = foul.foulVictim->GetTeam()->GetID();
    ScheduleRestart({now, foul.foulVictim->GetTeam(), stadium_to_home},
        foul.foulType >= 2 ? kCardAdministration : TickSpan{});
    if (foul.foulType == 2) {
      foul.foulPlayer->GiveYellowCard(now + kCardEffectDelay);
    }
    if (foul.foulType == 3) {
      foul.foulPlayer->GiveRedCard(now + kCardEffectDelay);
    }

    foul.hasBeenProcessed = true;

    return true;
  }

  return false;
}

void Referee::ScheduleRestart(const RestartSchedule& schedule, TickSpan administration) {
  const auto policy = PolicyFor(buffer.desiredSetPiece);
  RestartState state;
  state.entered_tick = schedule.now;
  state.earliest_restart_tick = state.entered_tick + policy.minimum_delay + administration;
  state.timeout_tick = state.entered_tick + policy.maximum_delay + administration;
  // Ordinary plans store only the fixed home-pitch target, never a stale runtime frame.
  buffer.restartPos = schedule.frame.Position(buffer.restartPos);
  buffer.stop_tick = state.entered_tick;
  buffer.prepare_tick = {}; // Ceremonial deadlines are not used for ordinary restarts.
  buffer.start_tick = {};
  buffer.setpiece_team = schedule.setpiece_team;
  buffer.taker = nullptr;
  buffer.endPhase = false;
  buffer.active = true;
  buffer.restart = std::move(state);
}

bool Referee::RestartNeedsSimulation() const {
  return buffer.active && buffer.restart && buffer.restart->phase == RestartPhase::Pending;
}

std::optional<Vector3> Referee::GetRestartTarget(const Player* player) const {
  if (!RestartNeedsSimulation()) return std::nullopt;
  for (const auto& target : buffer.restart->plan.players)
    if (target.player == player) return target.position;
  return std::nullopt;
}

void Referee::ProcessRestart(const RefereeTickFacts& facts, const MatchOptions& options,
                             blunted::Rng& rng, RuleCommandSink& commands) {
  auto& state = *buffer.restart;
  if (state.phase == RestartPhase::Taken) {
    state.phase = RestartPhase::InPlay;
    buffer.active = false;
    commands.StopSetPiece();
    post_restart_relax_ = kPostRestartRelax;
    foul.foulPlayer = nullptr;
    foul.foulType = 0;
    return;
  }
  if (state.phase != RestartPhase::Pending) return;
  if (!state.setup_done) {
    rng.Seed(options.game_engine_random_seed);
    if (buffer.desiredSetPiece == e_GameMode_FreeKick) {
      buffer.restartPos.coords[0] = clamp(buffer.restartPos.coords[0], -0.95f * facts.pitch.half_length(), 0.95f * facts.pitch.half_length());
      buffer.restartPos.coords[1] = clamp(buffer.restartPos.coords[1], -0.95f * facts.pitch.half_width(), 0.95f * facts.pitch.half_width());
    }
    commands.ResetSituation(facts.ball_to_home.Position(buffer.restartPos));
    std::vector<Player*> active;
    facts.home.GetActivePlayers(active);
    facts.away.GetActivePlayers(active);
    state.plan = PlanRestart(facts.pitch, facts.ball_to_home.Position(
        facts.ball.Predict(TickSpan{})), active, buffer.desiredSetPiece, *buffer.setpiece_team);
    for (const auto& actor : state.plan.players) {
      const auto frame = FromHomePitchFrame(*actor.player->GetTeam());
      actor.player->ResetPosition(actor.player->GetPosition(), frame.Position(state.plan.ball_position));
    }
    buffer.taker = state.plan.taker;
    state.setup_done = true;
  }
  if (!buffer.taker || !buffer.taker->IsActive()) {
    std::vector<Player*> active;
    facts.home.GetActivePlayers(active);
    facts.away.GetActivePlayers(active);
    state.plan = PlanRestart(facts.pitch, facts.ball_to_home.Position(
        facts.ball.Predict(TickSpan{})), active, buffer.desiredSetPiece, *buffer.setpiece_team);
    buffer.taker = state.plan.taker;
  }
  if (facts.now < state.earliest_restart_tick) return;
  if (!state.used_timeout_placement && facts.now >= state.timeout_tick) {
    PlaceRestartPlayersAtTimeout(state.plan);
    commands.ResetBall(facts.ball_to_home.Position(state.plan.ball_position));
    state.used_timeout_placement = true;
  }
  const auto ball_position = facts.ball_to_home.Position(facts.ball.Predict(TickSpan{}));
  if (!RestartPlayersReady(state.plan, facts.pitch) ||
      std::fabs(ball_position.coords[2] - 0.11f) > 0.03f ||
      (ball_position.Get2D() - state.plan.ball_position).GetLength() > 0.05f ||
      facts.ball.GetMovement().GetLength() > 0.5f) return;
  state.phase = RestartPhase::Ready;
  buffer.start_tick = facts.now; // Fact: authorization instant, not a preset delay.
  if (buffer.desiredSetPiece == e_GameMode_ThrowIn) {
    buffer.taker->SelectRetainAnim();
    commands.SetBallRetainer(buffer.taker);
  }
  commands.StartPlay();
  commands.StartSetPiece();
}
