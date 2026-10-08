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

#include "sim/rules/referee.hpp"
#include <cmath>
#include <algorithm>

#include "football/ball/ball.hpp"
#include "sim/player/player.hpp"
#include "sim/team/team.hpp"
#include "sim/pitch_geometry.hpp"
#include "sim/rules/offside.hpp"
#include "sim/observation/mentalimage.hpp"
#include "sim/rules/restart_placement.hpp"
#include "sim/observation/pitch_frame.hpp"

namespace {
using football::sim::Tick;
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

void Referee::Process(const RefereeTickFacts& facts, const MatchOptions& options,
                      blunted::Rng& rng, RuleCommandSink& commands) {
  if (facts.phase == MatchPhase::Finished) return;
  const auto team = [&](int id) { return id == 0 ? &facts.home : &facts.away; };
  // Track this operation's own stop consequences; do not query a runtime owner.
  bool play_authorized = facts.play_authorized;
  const auto check_foul = [&] {
    const bool stopped = CheckFoul(facts.now, facts.stadium_to_home, commands);
    if (stopped) play_authorized = false;
    return stopped;
  };
  if (buffer.active && buffer.restart) {
    ProcessRestart(facts, options, rng, commands);
    if (post_restart_relax_ > TickSpan{}) post_restart_relax_ = post_restart_relax_ - TickSpan{1};
    return;
  }
  if (play_authorized && !facts.set_piece_active) {
    const auto home_ball = facts.ball_to_home.Position(facts.ball.Predict(TickSpan{}));
    // Legacy foul/side data uses stadium coordinates, including switched ends.
    Vector3 ballPos = facts.stadium_to_home.Position(home_ball);

    // goal kick / corner

    if (fabs(ballPos.coords[0]) > pitchHalfW + lineHalfW + 0.11 ||
        facts.goal_scored) {

      foul.advantage = false;
      bool isFoul = false;
      if (!facts.goal_scored) isFoul = check_foul(); else foul.foulType = 0;
      if (isFoul == false) {

        commands.StopPlay();
        play_authorized = false;

        // corner, goal kick or kick off?
        Team *lastTouchTeam = facts.last_touch_team;
        if (lastTouchTeam == 0) lastTouchTeam = team(options.reverse_team_processing ? 1 : 0);
        signed int lastSide = lastTouchTeam->GetStaticSide();

        if (facts.goal_scored) {
          buffer.desiredSetPiece = e_GameMode_KickOff;
          buffer.restartPos = Vector3(0, 0, 0);
          buffer.teamID = facts.last_goal_team->Opponent()->GetID();
        } else if ((ballPos.coords[0] > 0 && lastSide > 0) ||
                   (ballPos.coords[0] < 0 && lastSide < 0)) {
          buffer.desiredSetPiece = e_GameMode_Corner;
          float y = ballPos.coords[1];
          if (y > 0) y = pitchHalfH; else
                     y = -pitchHalfH;
          buffer.restartPos = Vector3(pitchHalfW * lastSide, y, 0);
          buffer.teamID = 1 - lastTouchTeam->GetID();
        } else {
          buffer.desiredSetPiece = e_GameMode_GoalKick;
          buffer.restartPos = Vector3(pitchHalfW * 0.92 * -lastSide, 0, 0);
          buffer.teamID = 1 - lastTouchTeam->GetID();
        }

        ScheduleRestart({facts.now, team(buffer.teamID), facts.stadium_to_home});
      }
    }

    // over sideline

    if (play_authorized && post_restart_relax_ == TickSpan{}) {
      if (fabs(ballPos.coords[1]) > pitchHalfH + lineHalfW + 0.11) {
        foul.advantage = false;
        if (!check_foul()) {
          commands.StopPlay();
          play_authorized = false;
          Team *lastTouchTeam = facts.last_touch_team;
          if (lastTouchTeam == 0) lastTouchTeam = &facts.home;
          buffer.teamID = 1 - lastTouchTeam->GetID();
          buffer.desiredSetPiece = e_GameMode_ThrowIn;
          buffer.restartPos.coords[0] = clamp(ballPos.coords[0], -pitchHalfW + 0.6f, pitchHalfW - 0.6f);
          if (ballPos.coords[1] >  0) buffer.restartPos.coords[1] = pitchHalfH;
          if (ballPos.coords[1] <= 0) buffer.restartPos.coords[1] = -pitchHalfH;
          buffer.restartPos.coords[2] = 0;
          ScheduleRestart({facts.now, team(buffer.teamID), facts.stadium_to_home});
        }
      }
    }

    check_foul();

  } else {  // not in play, maybe something needs to happen?

    if (!play_authorized && !facts.set_piece_active && buffer.active == true) {

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

void Referee::PrepareCeremonialKickOff(const RefereeTickFacts& facts,
                                     const MatchOptions& options, blunted::Rng& rng,
                                     RuleCommandSink& commands) {
  // Opening/half-time placement is intentionally separate from ordinary readiness.
  commands.ResetSituation(options.reverse_team_processing ? -buffer.restartPos : buffer.restartPos);
  Team* first = facts.first_team == 0 ? &facts.home : &facts.away;
  Team* second = facts.first_team == 0 ? &facts.away : &facts.home;
  Player *first_taker = PositionRestartPlayers(first, e_GameMode_KickOff, second,
      buffer.setpiece_team->GetID(), buffer.teamID, facts.ball, facts.regulation, options, rng, commands);
  Player *second_taker = PositionRestartPlayers(second, e_GameMode_KickOff, first,
      buffer.setpiece_team->GetID(), buffer.teamID, facts.ball, facts.regulation, options, rng, commands);
  buffer.taker = buffer.teamID == facts.first_team ? first_taker : second_taker;
  offsidePlayers.clear();
}

void Referee::BallTouched(const football::sim::rules::BallTouchFacts& facts,
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
        if (!CheckFoul(facts.now, facts.stadium_to_home, commands)) {
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
    float offside = football::sim::rules::GetOffsideLine(mentalImage, facts.now,
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

void Referee::TripNotice(Player *tripee, Player *tripper, int tackleType,
                         Tick now, const Vector3& ball_position) {

  if (buffer.active) return;

  const PlayerActionState &tripperAction =
      tripper->GetSimulationActionState();

  if (tackleType == 2) {
      // standing tackle
    if (tripee->GetTeam()->GetFadingTeamPossessionAmount() > 1.1 &&
        (tripperAction.type == e_FunctionType_Interfere ||
         tripperAction.type == e_FunctionType_Sliding) &&
        (tripee->GetPosition() - ball_position.Get2D())
                .GetLength() < 2.0 &&
        tripper->GetTeam()->GetID() != tripee->GetTeam()->GetID()) {
      // uooooga uooooga foul!
      foul.foulType = 1;
      foul.advantage = true;
      foul.foulPlayer = tripper;
      foul.foulVictim = tripee;
      foul.foul_tick = now;
      foul.foulPosition = tripee->GetPitchPosition();
      foul.hasBeenProcessed = false;
    }

  } else if (tackleType == 3 &&
             (tripper != foul.foulPlayer || foul.foulType == 0)) {
      // sliding tackle

    if (now - tripper->GetLastTouchTick() > kTouchGrace &&
        tripperAction.type == e_FunctionType_Sliding &&
        tripper->GetTeam()->GetID() != tripee->GetTeam()->GetID() &&
        (ball_position - tripee->GetPosition()).GetLength() <
            8.0) {
      float severity = 1.0;
      if (tripperAction.HasScheduledContact()) {
        severity = std::pow(clamp(fabs(tripperAction.ContactFrame() -
                                       tripperAction.Frame()) /
                                      tripperAction.ContactFrame(),
                                  0.0, 1.0),
                            0.7) *
                   0.5;
        severity += NormalizedClamp(
            (ball_position - tripperAction.contactPosition)
                .GetLength(),
            0.0, 2.0) *
            0.5;
      }
      // from behind?
      severity += (tripee->GetPosition() - tripper->GetPosition()).GetNormalized(0).GetDotProduct(tripee->GetDirectionVec()) * 0.5 + 0.5;

      if (severity > 1.0) {
        // uooooga uooooga foul!
        foul.foulType = 1;
        foul.advantage = false;
        foul.foulPlayer = tripper;
        foul.foulVictim = tripee;
        foul.foul_tick = now;
        foul.foulPosition = tripee->GetPitchPosition();
        foul.hasBeenProcessed = false;
        if (severity > 1.4) foul.foulType = 2;
        if (severity > 2.0) {
          foul.foulType = 3;
        }
      }
    }
  }
}


bool Referee::CheckFoul(Tick now, PitchFrameTransform stadium_to_home,
                        RuleCommandSink& commands) {

  bool penalty = false;
  if (foul.foulType != 0) {
    if (fabs(foul.foulPosition.coords[1]) < 20.15 - lineHalfW &&
        foul.foulPosition.coords[0] *
                -foul.foulVictim->GetTeam()->GetStaticSide() >
            pitchHalfW - 16.5 + lineHalfW)
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
        ? Vector3((pitchHalfW - 11.0) * foul.foulPlayer->GetTeam()->GetStaticSide(),
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
      buffer.restartPos.coords[0] = clamp(buffer.restartPos.coords[0], -0.95f * pitchHalfW, 0.95f * pitchHalfW);
      buffer.restartPos.coords[1] = clamp(buffer.restartPos.coords[1], -0.95f * pitchHalfH, 0.95f * pitchHalfH);
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
