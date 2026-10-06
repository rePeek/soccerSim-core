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

#include "sim/referee.hpp"
#include <cmath>

#include "sim/match.hpp"
#include "sim/rules/offside.hpp"
#include "sim/ai_support/mentalimage.hpp"
#include "sim/rules/restart_placement.hpp"

namespace {
using football::sim::Tick;
using football::sim::TickSpan;
using football::sim::Seconds;
// Unit-only migration of the existing policy. These legacy event intervals are
// not the final readiness-driven restart model.
constexpr auto kCardAdministration = Seconds(10);
constexpr auto kRestartPreparation = Seconds(2);
constexpr auto kRestartWhistle = Seconds(2);
constexpr TickSpan kGoalPreparation{50};
constexpr TickSpan kGoalWhistle{50};
constexpr TickSpan kHalfPreparation{10};
constexpr TickSpan kHalfWhistle{20};
constexpr TickSpan kPostRestartRelax{40};
constexpr TickSpan kTouchGrace{60};
constexpr TickSpan kAdvantageRecheck{60};
constexpr auto kAdvantageExpiry = Seconds(3);
constexpr auto kCardEffectDelay = Seconds(6);
}  // namespace

Referee::Referee(Match *match) : match(match) {
  buffer.desiredSetPiece = e_GameMode_KickOff;
  buffer.teamID = match->FirstTeam();
  buffer.setpiece_team = match->GetTeam(match->FirstTeam());
  buffer.stop_tick = {};
  buffer.prepare_tick = {};
  buffer.start_tick = Tick{} + kRestartWhistle;
    buffer.restartPos = match->options().ball_position;
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

bool Referee::PeriodElapsed() const {
  const auto limit = match->GetMatchPhase() == MatchPhase::FirstHalf ? 1u
      : match->GetMatchPhase() == MatchPhase::SecondHalf ? 2u : 0u;
  return limit != 0 &&
         match->GetMatchTime_ms() >=
             match->options().half_duration_ms * limit;
}

void Referee::Process() {
  if (match->Finished()) return;
  // Period authority is independent of restart eligibility. A pending set piece
  // must not keep a match alive after the regulation clock reaches full time.
  if (PeriodElapsed()) {
    match->StopPlay();
    match->StopSetPiece();
    buffer.active = false;
    buffer.taker = nullptr;
    buffer.endPhase = false;
    if (match->GetMatchPhase() == MatchPhase::SecondHalf) {
      match->SetMatchPhase(MatchPhase::Finished);
      return;
    }
    // Half time: abandon any pending foul, then schedule the second-half kickoff
    // and change ends at the next tick's canonical frame, before the kickoff reset
    // repositions the actors.
    foul.foulPlayer = nullptr;
    foul.foulType = 0;
    foul.advantage = false;
    foul.foul_tick = {};
    foul.hasBeenProcessed = true;
    buffer.desiredSetPiece = e_GameMode_KickOff;
    buffer.stop_tick = match->GetTimelineTick();
    buffer.prepare_tick = buffer.stop_tick + kHalfPreparation;
    buffer.start_tick = buffer.prepare_tick + kHalfWhistle;
    buffer.restartPos = match->options().ball_position;
    buffer.active = true;
    buffer.endPhase = true;
    buffer.teamID = match->options().left_team_owns_ball ? 1 : 0;
    buffer.setpiece_team = match->GetTeam(buffer.teamID);
    match->SetMatchPhase(MatchPhase::SecondHalf);
    match->RequestChangeOfEnds();
    return;
  }
  if (match->IsInPlay() && !match->IsInSetPiece()) {
    Vector3 ballPos = match->GetBall()->Predict(0);

    // We process corner setup in not mirrored setup.
    if (match->options().reverse_team_processing) {
      ballPos.Mirror();
    }

    // goal kick / corner

    if (fabs(ballPos.coords[0]) > pitchHalfW + lineHalfW + 0.11 ||
        match->IsGoalScored()) {

      foul.advantage = false;
      bool isFoul = false;
      if (!match->IsGoalScored()) isFoul = CheckFoul(); else foul.foulType = 0;
      if (isFoul == false) {

        match->StopPlay();

        // corner, goal kick or kick off?
        Team *lastTouchTeam = match->GetLastTouchTeam();
        if (lastTouchTeam == 0) lastTouchTeam = match->GetTeam(match->options().reverse_team_processing ? 1 : 0);
        signed int lastSide = lastTouchTeam->GetStaticSide();

        if (match->IsGoalScored()) {
          buffer.desiredSetPiece = e_GameMode_KickOff;
          buffer.stop_tick = match->GetTimelineTick();
          buffer.prepare_tick = buffer.stop_tick + kGoalPreparation;
          match->AdvanceToRestartPreparation(buffer.prepare_tick);
          buffer.start_tick = buffer.prepare_tick + kGoalWhistle;
          buffer.restartPos = Vector3(0, 0, 0);
          buffer.teamID = match->FirstTeam();
          buffer.setpiece_team = match->GetLastGoalTeam()->Opponent();
        } else if ((ballPos.coords[0] > 0 && lastSide > 0) ||
                   (ballPos.coords[0] < 0 && lastSide < 0)) {
          buffer.desiredSetPiece = e_GameMode_Corner;
          buffer.stop_tick = match->GetTimelineTick();
          buffer.prepare_tick = buffer.stop_tick + kRestartPreparation;
          match->AdvanceToRestartPreparation(buffer.prepare_tick);
          buffer.start_tick = buffer.prepare_tick + kRestartWhistle;
          float y = ballPos.coords[1];
          if (y > 0) y = pitchHalfH; else
                     y = -pitchHalfH;
          buffer.restartPos = Vector3(pitchHalfW * lastSide, y, 0);
          buffer.teamID = 1 - lastTouchTeam->GetID();
        } else {
          buffer.desiredSetPiece = e_GameMode_GoalKick;
          buffer.stop_tick = match->GetTimelineTick();
          buffer.prepare_tick = buffer.stop_tick + kRestartPreparation;
          match->AdvanceToRestartPreparation(buffer.prepare_tick);
          buffer.start_tick = buffer.prepare_tick + kRestartWhistle;
          buffer.restartPos = Vector3(pitchHalfW * 0.92 * -lastSide, 0, 0);
          buffer.teamID = 1 - lastTouchTeam->GetID();
        }

        buffer.active = true;
        buffer.taker = nullptr;  // No prepared taker until this restart's deadline.
      }
    }

    // over sideline

    if (post_restart_relax_ == TickSpan{}) {
      if (fabs(ballPos.coords[1]) > pitchHalfH + lineHalfW + 0.11) {
        foul.advantage = false;
        if (!CheckFoul()) {
          match->StopPlay();
          Team *lastTouchTeam = match->GetLastTouchTeam();
          if (lastTouchTeam == 0) lastTouchTeam = match->GetTeam(0);
          buffer.teamID = 1 - lastTouchTeam->GetID();
          buffer.desiredSetPiece = e_GameMode_ThrowIn;
          buffer.stop_tick = match->GetTimelineTick();
          buffer.prepare_tick = buffer.stop_tick + kRestartPreparation;
          match->AdvanceToRestartPreparation(buffer.prepare_tick);
          buffer.start_tick = buffer.prepare_tick + kRestartWhistle;
          buffer.restartPos.coords[0] = clamp(ballPos.coords[0], -pitchHalfW + 0.6f, pitchHalfW - 0.6f);
          if (ballPos.coords[1] >  0) buffer.restartPos.coords[1] = pitchHalfH;
          if (ballPos.coords[1] <= 0) buffer.restartPos.coords[1] = -pitchHalfH;
          buffer.restartPos.coords[2] = 0;
          buffer.active = true;
          buffer.taker = nullptr;
        }
      }
    }

    CheckFoul();

  } else {  // not in play, maybe something needs to happen?

    if (!match->IsInPlay() && !match->IsInSetPiece() && buffer.active == true) {

      if (buffer.taker == nullptr && match->GetTimelineTick() >= buffer.prepare_tick) {
        if (buffer.endPhase == true) {
          if (match->GetMatchPhase() == MatchPhase::PreMatch) {
            match->SetMatchPhase(MatchPhase::FirstHalf);
          }
          buffer.endPhase = false;
        }

        // Deterministic reseed before positioning players for every restart.
        match->rng().Seed(match->options().game_engine_random_seed);
        PrepareSetPiece(buffer.desiredSetPiece);
      }

      if (buffer.taker != nullptr && match->GetTimelineTick() >= buffer.start_tick) {
        // blow whistle and wait for set piece taker to touch the ball
        match->StartPlay();
        match->StartSetPiece();
      }
    }
  }

  if (match->IsInSetPiece()) {
    // check if set piece has been taken
    if (buffer.desiredSetPiece == e_GameMode_KickOff ||
        (buffer.taker->GetSimulationActionState().HasScheduledContact() &&
         !buffer.taker->GetSimulationActionState().IsContactPending())) {
      buffer.active = false;
      match->StopSetPiece();
      post_restart_relax_ = kPostRestartRelax;
      foul.foulPlayer = 0;
      foul.foulType = 0;

      if (match->GetMatchPhase() == MatchPhase::PreMatch) {
        match->SetMatchPhase(MatchPhase::FirstHalf);
      }
    }
  }

  if (post_restart_relax_ > TickSpan{})
    post_restart_relax_ = post_restart_relax_ - TickSpan{1};
}

void Referee::PrepareSetPiece(e_GameMode setPiece) {
  // position players for set piece situation
  if (setPiece == e_GameMode_FreeKick) {
    buffer.restartPos.coords[0] = clamp(buffer.restartPos.coords[0],
                                        -0.95 * pitchHalfW, 0.95 * pitchHalfW);
    buffer.restartPos.coords[1] = clamp(buffer.restartPos.coords[1],
                                        -0.95 * pitchHalfH, 0.95 * pitchHalfH);
  }
  match->ResetSituation(match->options().reverse_team_processing
                            ? -buffer.restartPos
                            : buffer.restartPos);

  Player *first_taker = PositionRestartPlayers(match->GetTeam(match->FirstTeam()),
      setPiece, match->GetTeam(match->SecondTeam()),
      buffer.setpiece_team->GetID(), buffer.teamID);
  Player *second_taker = PositionRestartPlayers(match->GetTeam(match->SecondTeam()),
      setPiece, match->GetTeam(match->FirstTeam()),
      buffer.setpiece_team->GetID(), buffer.teamID);
  buffer.taker = buffer.teamID == match->FirstTeam() ? first_taker : second_taker;
  // Reset offside state.
  offsidePlayers.clear();
}

void Referee::BallTouched() {

  if (!match->options().offsides) {
    return;
  }
  // check for offside player receiving the ball

  int lastTouchTeamID = match->GetLastTouchTeamID();
  if (lastTouchTeamID == -1) return; // shouldn't happen really ;)
  if (match->IsInPlay() && !match->IsInSetPiece() && buffer.active == false &&
      match->GetTeam(1 - lastTouchTeamID)->GetActivePlayersCount() > 1) {
      // disable if only 1 player: that's debug mode with only
                    // keeper
    auto ballOwner = match->GetLastTouchPlayer();
    for (auto p : offsidePlayers) {
      if (p == ballOwner) {
        foul.advantage = false;
        if (!CheckFoul()) {
          // uooooga uooooga offside!
          match->StopPlay();
          buffer.desiredSetPiece = e_GameMode_FreeKick;
          buffer.stop_tick = match->GetTimelineTick();
          buffer.prepare_tick = buffer.stop_tick + kRestartPreparation;
          match->AdvanceToRestartPreparation(buffer.prepare_tick);
          buffer.start_tick = buffer.prepare_tick + kRestartWhistle;
          buffer.restartPos = ballOwner->GetPitchPosition();
          buffer.teamID = 1 - lastTouchTeamID;
          buffer.active = true;
          buffer.taker = nullptr;
        }
      }
    }
  }

  offsidePlayers.clear();

  if (match->IsInPlay() &&
      (buffer.active == false ||
       (buffer.active == true && buffer.desiredSetPiece != e_GameMode_ThrowIn &&
        buffer.desiredSetPiece != e_GameMode_Corner))) {
    // check for offside players at moment of touch
    MentalImage mentalImage(match);
    float offside = football::sim::rules::GetOffsideLine(match, &mentalImage, 1 - lastTouchTeamID);
    std::vector<Player*> players;
    Team *team = match->GetTeam(lastTouchTeamID);
    team->GetActivePlayers(players);
    for (auto player : players) {
      if (player != team->GetLastTouchPlayer()) {
        if (player->GetPosition().coords[0] * team->GetDynamicSide() <
            offside * team->GetDynamicSide()) {
          offsidePlayers.push_back(player);
        }
      }
    }
  }
}

void Referee::TripNotice(Player *tripee, Player *tripper, int tackleType) {

  if (buffer.active) return;

  const PlayerActionState &tripperAction =
      tripper->GetSimulationActionState();

  if (tackleType == 2) {
      // standing tackle
    if (tripee->GetTeam()->GetFadingTeamPossessionAmount() > 1.1 &&
        (tripperAction.type == e_FunctionType_Interfere ||
         tripperAction.type == e_FunctionType_Sliding) &&
        (tripee->GetPosition() - match->GetBall()->Predict(0).Get2D())
                .GetLength() < 2.0 &&
        tripper->GetTeam()->GetID() != tripee->GetTeam()->GetID()) {
      // uooooga uooooga foul!
      foul.foulType = 1;
      foul.advantage = true;
      foul.foulPlayer = tripper;
      foul.foulVictim = tripee;
      foul.foul_tick = match->GetTimelineTick();
      foul.foulPosition = tripee->GetPitchPosition();
      foul.hasBeenProcessed = false;
    }

  } else if (tackleType == 3 &&
             (tripper != foul.foulPlayer || foul.foulType == 0)) {
      // sliding tackle

    // Temporary boundary to the not-yet-migrated Player touch timestamp.
    const Tick last_touch{football::sim::TickSpanFromMillisecondsExact(
        tripper->GetLastTouchTime_ms()).value};
    if (match->GetTimelineTick() - last_touch > kTouchGrace &&
        tripperAction.type == e_FunctionType_Sliding &&
        tripper->GetTeam()->GetID() != tripee->GetTeam()->GetID() &&
        (match->GetBall()->Predict(0) - tripee->GetPosition()).GetLength() <
            8.0) {
      float severity = 1.0;
      if (tripperAction.HasScheduledContact()) {
        severity = std::pow(clamp(fabs(tripperAction.contactFrame -
                                       tripperAction.frame) /
                                      tripperAction.contactFrame,
                                  0.0, 1.0),
                            0.7) *
                   0.5;
        severity += NormalizedClamp(
            (match->GetBall()->Predict(0) - tripperAction.contactPosition)
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
        foul.foul_tick = match->GetTimelineTick();
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


bool Referee::CheckFoul() {

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
      if (match->GetTimelineTick() > foul.foul_tick + kAdvantageRecheck) {
        if (match->GetTimelineTick() > foul.foul_tick + kAdvantageExpiry) {
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

    match->StopPlay();
    buffer.desiredSetPiece = penalty ? e_GameMode_Penalty : e_GameMode_FreeKick;
    buffer.stop_tick = match->GetTimelineTick();
    buffer.prepare_tick = buffer.stop_tick + kRestartPreparation;
    if (foul.foulType >= 2) buffer.prepare_tick += kCardAdministration;
    match->AdvanceToRestartPreparation(buffer.prepare_tick);
    buffer.start_tick = buffer.prepare_tick + kRestartWhistle;
    buffer.restartPos = penalty
        ? Vector3((pitchHalfW - 11.0) * foul.foulPlayer->GetTeam()->GetStaticSide(),
                  0, 0)
        : foul.foulPosition;
    buffer.teamID = foul.foulVictim->GetTeam()->GetID();
    buffer.active = true;
    buffer.taker = nullptr;
    if (foul.foulType == 2) {
      foul.foulPlayer->GiveYellowCard(football::sim::ToMilliseconds(
          match->GetTimelineTick() + kCardEffectDelay)); // Player migrates separately.
    }
    if (foul.foulType == 3) {
      foul.foulPlayer->GiveRedCard(football::sim::ToMilliseconds(
          match->GetTimelineTick() + kCardEffectDelay));
    }

    foul.hasBeenProcessed = true;

    return true;
  }

  return false;
}
