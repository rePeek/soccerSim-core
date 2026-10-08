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


#include "sim/rules/restart_placement.hpp"
#include "sim/rules/rule_command_sink.hpp"
#include "sim/query/player_query.hpp"
#include "football/ball/ball.hpp"
#include "sim/pitch_geometry.hpp"
#include "sim/team/team.hpp"
#include "sim/player/player.hpp"
#include <cmath>

namespace {
// Coordinate deformation for rule-owned restart layouts only. It no longer
// invokes an AI policy during authoritative reset/placement.
Vector3 AdaptRestartFormation(
    Player *player, float backXBound, float frontXBound,
    float lowYBound, float highYBound, float xFocus, float xFocusStrength,
    float yFocus, float yFocusStrength, const Vector3 &microFocus,
    float microFocusStrength, float midfieldFocus, float midfieldFocusStrength,
    bool useDynamicFormationPosition) {
  Team *team = player->GetTeam();
  int side = team->GetDynamicSide();

  Vector3 position;

  if (useDynamicFormationPosition) {
    position = player->GetDynamicFormationEntry().position;
  } else {
    position = player->GetFormationEntry().position;
  }

  // stretch midfield into defending or attack position
  if (midfieldFocusStrength > 0.0f) {
    float midfieldPositionFactor = midfieldFocus * 2.0f - 1.0f; // -1 .. 1

    // only for midfielders
    float stretchBias = clamp(1.0f - std::fabs(position.coords[0] * 1.2f), 0.0f, 1.0f); // overstretch a bit so defenders/attackers are left alone (they usually aren't fully x = 0 or 1)
    stretchBias = curve(stretchBias, 1.0f);

    stretchBias *= midfieldFocusStrength;
    //if (player->GetTeam()->GetID() == 0) printf("==> %f\n", position.coords[0]);
    position.coords[0] = position.coords[0] * (1.0f - stretchBias) + midfieldPositionFactor * stretchBias;
    //if (player->GetTeam()->GetID() == 0) printf("--> %f %f %f\n", position.coords[0], midfieldPositionFactor, stretchBias);
  }

  float xLength = frontXBound - backXBound;
  position.coords[0] = backXBound + (position.coords[0] * 0.5f + 0.5f) * xLength;
  float yLength = highYBound - lowYBound;
  position.coords[1] = lowYBound + (position.coords[1] * -side * 0.5f + 0.5f) * yLength;

  Vector3 purePosition = position;

  if (xFocusStrength > 0.0f) {
    float bias = 1.0f - clamp( std::fabs(xFocus - position.coords[0]) / std::fabs(backXBound - frontXBound) , 0.0f, 1.0f);
    bias = -std::cos(bias * pi) * 0.5f + 0.5f;
    bias = std::pow(bias, 0.8);
    bias *= xFocusStrength;
    position.coords[0] = position.coords[0] * (1.0f - bias) + xFocus * bias;
  }

  if (yFocusStrength > 0.0f) {
    float distance = clamp( std::fabs(yFocus - position.coords[1]) / std::fabs(highYBound - lowYBound) , 0.0f, 1.0f);

    float bias = 1.0f - distance;
    bias *= 0.2f + 0.8f * std::fabs(yFocus) / pitchHalfH;
    bias *= yFocusStrength;

    position.coords[1] = position.coords[1] * (1.0f - bias) + yFocus * bias;
  }

  // microfocus
  if (microFocusStrength > 0.0f) {

    float homogeneousYInfluenceBias = 0.2f; // 1.0f == act as if everybody is on the same Y position (so, as if we're in 1D, with only X)
    float homogeneousYPositionBias = 0.4f; // 1.0f == don't influence player's resulting Y position (so, as if we're in 1D, with only X)

    // this way, players are more strictly keeping to their positions
    float distStatic = ((microFocus - purePosition) * Vector3(1, 1.0f - homogeneousYInfluenceBias, 0)).GetLength() / 50.0f;//26.0f;//32.0f;
    // this way, microfocus gets more direct attention from players around the action - they kinda 'forget' their base positions a bit though
    //float distDynamic = (microFocus - player->GetPosition()).GetLength() / 34.0f;

    float dist = distStatic;// * 0.3f + distDynamic * 0.7f;
    //dist = std::min(dist, 0.7f); // always some microfocus (exp)

    if (dist < 1.0f) {
      /*
      // wolfram alpha: (std::sin((x + 0.5) * pi) * 0.5 + 0.5) * 0.7 + (std::sin((x - 0.25) * 2.0 * pi) * 0.5 + 0.5) * 0.3 | from x = 0 to 1
      float microFocusBias1 = std::sin((dist + 0.5f) * pi) * 0.5f + 0.5f; // -\_
      float microFocusBias2 = std::sin((dist - 0.25f) * 2.0f * pi) * 0.5f + 0.5f; // _/-\_
      // mates in team in possession want to keep some distance for easier pass-to-ability, while defenders just want to jump into the action more

      float sineBias = 0.15f;// + clamp(player->GetTeam()->GetFadingTeamPossessionAmount() - 0.5f, 0.0f, 1.0f) * 0.2f;
      float microFocusBias = microFocusBias1 * (1.0f - sineBias) +
                             microFocusBias2 * sineBias;
      */

      float microFocusBias = 1.0f;

      // less serious when far away
      // we need this, because we use bias to vary between pos and microfocuspos. so for far away players, microfocuspos has way more influence since it's farther away.
      microFocusBias *= 1.0f - dist;

      // more of a binary choice to come over completely or not at all
      microFocusBias = curve(microFocusBias, 0.3f);

      // more bulgy curve
      //microFocusBias = std::pow(microFocusBias, 0.9f);

      // extra short distance peak
      float peakLocation = 0.15f;
      float peakWidth = 0.25f;
      float peakHeight = 0.1f;
      microFocusBias += (1.0f - NormalizedClamp(std::fabs(dist - peakLocation), 0.0f, peakWidth)) * peakHeight;
      microFocusBias = clamp(microFocusBias, 0.0f, 1.0f);

      // 'compressor' (wolfram alpha: x, (x^0.7) * 0.7 | from x = 0 to 1)
      // microFocusBias = std::pow(microFocusBias, 0.7f);
      // microFocusBias = microFocusBias * 0.7f;

      //printf("%f\n", microFocusStrength);
      microFocusBias *= microFocusStrength;
      //microFocusBias = clamp(microFocusBias, 0.0f, 0.1f);

      Vector3 microFocusPosition = microFocus * Vector3(1, 1.0f - homogeneousYPositionBias, 1) + position * Vector3(0, homogeneousYPositionBias, 0);
      position = position * (1.0f - microFocusBias) + microFocusPosition * (microFocusBias);
    }
  }

  return position;
}


}  // namespace

Player *PositionRestartPlayers(Team *team, e_GameMode setPiece, Team *other_team,
                                       int kickoffTakerTeamId,
                                       int takerTeamID, const Ball& ball,
                                       football::sim::TickSpan regulation,
                                       const MatchOptions& options, blunted::Rng& rng,
                                       football::sim::rules::RuleCommandSink& commands) {
  Player *taker = nullptr;

  if (takerTeamID == -1) assert(setPiece == e_GameMode_Normal);
  if (setPiece == e_GameMode_Normal) return nullptr;
  std::vector<Player*> players;
  team->GetActivePlayers(players);

  std::vector<Player*>::iterator iter = players.begin();
  while (iter != players.end()) {
    Vector3 focus = ball.Predict(0).Get2D();
    if ((*iter)->GetFormationEntry().role == e_PlayerRole_GK) {
      if (setPiece == e_GameMode_KickOff) {
        focus.coords[0] = 0;
        focus.coords[1] = 0;
      }
      (*iter)->ResetPosition(
          Vector3(pitchHalfW * team->GetDynamicSide() * 0.98, 0, 0), focus);
      iter = players.erase(iter);
    } else {
      iter++;
    }
  }

  bool isTakerTeam = takerTeamID == team->GetID() ? true : false;

  if (isTakerTeam) team->SetFadingTeamPossessionAmount(1.5);
              else team->SetFadingTeamPossessionAmount(0.5);

              switch (setPiece) {

                case e_GameMode_KickOff:
                  for (unsigned int i = 0; i < players.size(); i++) {
                    Vector3 basePos =
                        players[i]->GetFormationEntry().position *
                        Vector3(-team->GetDynamicSide() * pitchHalfW * 0.6,
                                -team->GetDynamicSide() * pitchHalfH * 0.6, 0);
                    basePos.coords[1] +=
                        rng.Uniform(-2.0f,
                                    2.0f);  // to stop people from bumping into
                                            // each other and such
                    basePos.coords[0] *= 0.5;
                    basePos.coords[0] +=
                        (pitchHalfW * 0.2) * team->GetDynamicSide();
                    if (basePos.coords[0] * team->GetDynamicSide() < 0.5)
                      basePos.coords[0] =
                          0.5 * team->GetDynamicSide();  // not allowed to stand
                                                         // on opp side
                    if (basePos.GetLength() < 9.4) {
                        // not allowed to stand in center spot
                      basePos.Normalize(Vector3(team->GetDynamicSide(), 0, 0));
                      basePos *= 9.4;
                    }
                    players[i]->ResetPosition(
                        basePos, ball.Predict(0).Get2D());

                    // supporting players
                    if (isTakerTeam) {
                      std::vector<Player *> result;
                      football::sim::query::GetClosestPlayers(team, Vector3(0), result, 2);
                      for (unsigned int i = 0; i < result.size(); i++) {
                        result[i]->ResetPosition(
                            Vector3(0, i * 1.4 * team->GetDynamicSide(), 0),
                            ball.Predict(0).Get2D());
                      }
                    }
                  }
                  break;

                case e_GameMode_GoalKick:
                  for (unsigned int i = 0; i < players.size(); i++) {
                    float backXBound, frontXBound, lowYBound, highYBound;
                    if (isTakerTeam) {
                      backXBound = team->GetDynamicSide() * pitchHalfW * 0.5;
                      frontXBound = -team->GetDynamicSide() * pitchHalfW * 0.2;
                    } else {
                      backXBound = team->GetDynamicSide() * pitchHalfW * 0.4;
                      frontXBound = -team->GetDynamicSide() * pitchHalfW * 0.1;
                    }
                    lowYBound = -pitchHalfH * 0.7;
                    highYBound = pitchHalfH * 0.7;
                    Vector3 basePos = AdaptRestartFormation(
                        players[i], backXBound, frontXBound, lowYBound,
                        highYBound, 0, 0, 0, 0, 0, 0, 0, 0, false);
                    players[i]->ResetPosition(
                        basePos, ball.Predict(0).Get2D());
                  }
                  break;

                case e_GameMode_Corner:
                  for (unsigned int i = 0; i < players.size(); i++) {
                    float backXBound, frontXBound, lowYBound, highYBound,
                        xFocus, xFocusStrength, yFocus, yFocusStrength,
                        midfieldFocus, midfieldFocusStrength;
                    Vector3 ballPos = ball.Predict(0).Get2D();
                    if (isTakerTeam) {
                      backXBound = -team->GetDynamicSide() * pitchHalfW * 0.2;
                      frontXBound = -team->GetDynamicSide() * pitchHalfW * 0.96;
                      xFocus = frontXBound * 0.85;
                      xFocusStrength = 0.7f;
                      yFocus = ballPos.coords[1] * 0.1f;
                      yFocusStrength = 0.7f;
                      midfieldFocus = 0.9f;
                      midfieldFocusStrength = 0.5f;
                    } else {
                      backXBound = team->GetDynamicSide() * pitchHalfW * 0.98;
                      frontXBound = team->GetDynamicSide() * pitchHalfW * 0.5;
                      xFocus = backXBound * 0.94;
                      xFocusStrength = 0.8f;
                      yFocus = ballPos.coords[1] * 0.1f;
                      yFocusStrength = 0.9f;
                      midfieldFocus = 0.1f;
                      midfieldFocusStrength = 0.7f;
                    }
                    lowYBound = -pitchHalfH * 0.6;
                    highYBound = pitchHalfH * 0.6;
                    Vector3 basePos = AdaptRestartFormation(
                        players[i], backXBound, frontXBound, lowYBound,
                        highYBound, xFocus, xFocusStrength, yFocus,
                        yFocusStrength,
                        Vector3(ballPos.coords[0] * 0.95f,
                                ballPos.coords[1] * 0.1f, 0),
                        0.9, midfieldFocus, midfieldFocusStrength, false);
                    players[i]->ResetPosition(
                        basePos, ball.Predict(0).Get2D());
                  }
                  break;

                case e_GameMode_ThrowIn:
                  for (unsigned int i = 0; i < players.size(); i++) {
                    float backXBound, frontXBound, lowYBound, highYBound,
                        xFocus, xFocusStrength, yFocus, yFocusStrength;
                    Vector3 ballPos = ball.Predict(0).Get2D();
                    if (isTakerTeam) {
                      backXBound =
                          clamp(ballPos.coords[0] + 30 * team->GetDynamicSide(),
                                -pitchHalfW, pitchHalfW);
                      frontXBound = clamp(
                          ballPos.coords[0] + 20 * -team->GetDynamicSide(),
                          -pitchHalfW, pitchHalfW);
                      xFocus =
                          clamp(ballPos.coords[0] + 4 * -team->GetDynamicSide(),
                                -pitchHalfW, pitchHalfW);
                      xFocusStrength = 0.4;
                      yFocus = ballPos.coords[1] * 0.996;
                      yFocusStrength = 0.6;
                    } else {
                      backXBound =
                          clamp(ballPos.coords[0] + 30 * team->GetDynamicSide(),
                                -pitchHalfW, pitchHalfW);
                      frontXBound = clamp(
                          ballPos.coords[0] + 15 * -team->GetDynamicSide(),
                          -pitchHalfW, pitchHalfW);
                      xFocus =
                          clamp(ballPos.coords[0] + 16 * team->GetDynamicSide(),
                                -pitchHalfW, pitchHalfW);
                      xFocusStrength = 0.2;
                      yFocus = ballPos.coords[1] * 0.95;
                      yFocusStrength = 0.5;
                    }
                    lowYBound = -pitchHalfH * 0.75f + ballPos.coords[1] * 0.25f;
                    highYBound = pitchHalfH * 0.75f + ballPos.coords[1] * 0.25f;
                    Vector3 basePos = AdaptRestartFormation(
                        players[i], backXBound, frontXBound, lowYBound,
                        highYBound, xFocus, xFocusStrength, yFocus,
                        yFocusStrength, ballPos, 0.7, 0, 0, false);
                    players[i]->ResetPosition(
                        basePos, ball.Predict(0).Get2D());
                  }
                  break;

                case e_GameMode_FreeKick:
                  for (unsigned int i = 0; i < players.size(); i++) {
                    float backXBound, frontXBound, lowYBound, highYBound,
                        xFocus, xFocusStrength, yFocus, yFocusStrength;
                    Vector3 ballPos = ball.Predict(0).Get2D();
                    if (isTakerTeam) {
                      float xOffset =
                          clamp((ballPos.coords[0] * -team->GetDynamicSide()) /
                                    pitchHalfW,
                                -1.0, 1.0) *
                              0.5 +
                          0.5;  // 0 == close to our goal, 1 == far from our
                                // goal
                      backXBound = clamp(team->GetDynamicSide() * pitchHalfW *
                                             (0.7 - xOffset * 0.7),
                                         -pitchHalfW, pitchHalfW);
                      frontXBound = clamp(team->GetDynamicSide() * pitchHalfW *
                                              (-0.3 - xOffset * 0.7),
                                          -pitchHalfW, pitchHalfW);
                      // printf("fb: %f %f\n", backXBound, frontXBound);
                      xFocus = clamp(
                          ballPos.coords[0] + 10 * -team->GetDynamicSide(),
                          -pitchHalfW, pitchHalfW);
                      xFocusStrength = 0.5 + xOffset * 0.2;
                      yFocus = ballPos.coords[1] * 0.4;
                      yFocusStrength = 0.6 + xOffset * 0.2;
                    } else {
                      float xOffset =
                          clamp((ballPos.coords[0] * -team->GetDynamicSide()) /
                                    pitchHalfW,
                                -1.0, 1.0) *
                              0.5 +
                          0.5;  // 0 == close to our goal, 1 == far from our
                                // goal
                      // xOffset *= 40.0;
                      backXBound = clamp(team->GetDynamicSide() * pitchHalfW *
                                             (1.0 - xOffset * 0.6),
                                         -pitchHalfW, pitchHalfW);
                      frontXBound = clamp(team->GetDynamicSide() * pitchHalfW *
                                              (0.5 - xOffset * 0.8),
                                          -pitchHalfW, pitchHalfW);
                      xFocus =
                          clamp(ballPos.coords[0] + 20 * team->GetDynamicSide(),
                                -pitchHalfW, pitchHalfW);
                      xFocusStrength = 0.6 - xOffset * 0.4;
                      yFocus = ballPos.coords[1] * 0.2;
                      yFocusStrength = 0.8 - xOffset * 0.4;
                    }
                    lowYBound = -pitchHalfH * 0.7;
                    highYBound = pitchHalfH * 0.7;
                    Vector3 basePos = AdaptRestartFormation(
                        players[i], backXBound, frontXBound, lowYBound,
                        highYBound, xFocus, xFocusStrength, yFocus,
                        yFocusStrength, ballPos, 0.4, 0, 0, false);

                    // keep distance
                    if (!isTakerTeam) {
                      if ((basePos - ball.Predict(0).Get2D())
                              .GetLength() < 9.15) {
                        basePos =
                            ball.Predict(0).Get2D() +
                            (basePos - ball.Predict(0).Get2D())
                                    .GetNormalized() *
                                9.15;
                      }
                    }

                    players[i]->ResetPosition(
                        basePos, ball.Predict(0).Get2D());
                  }

                  // wall
                  if (!isTakerTeam &&
                      (ball.Predict(0).Get2D() -
                       Vector3(team->GetDynamicSide() * pitchHalfW, 0, 0))
                              .GetLength() < 40.0) {
                    std::vector<Player *> result;
                    football::sim::query::GetClosestPlayers(team,
                                         ball.Predict(0).Get2D(),
                                         result, 3);
                    for (unsigned int i = 0; i < result.size(); i++) {
                      Vector3 toGoal =
                          (Vector3(team->GetDynamicSide() * pitchHalfW, 0, 0) -
                           ball.Predict(0).Get2D())
                              .GetNormalized(0);
                      toGoal += Vector3(0, 1.0 - i, 0) * 0.07;
                      toGoal.Normalize();
                      result[i]->ResetPosition(
                          ball.Predict(0).Get2D() + toGoal * 9.15f,
                          ball.Predict(0).Get2D());
                    }
                  }

                  break;

                case e_GameMode_Penalty:
                  for (unsigned int i = 0; i < players.size(); i++) {
                    float backXBound, frontXBound, lowYBound, highYBound,
                        xFocus, xFocusStrength, yFocus, yFocusStrength;
                    Vector3 ballPos = ball.Predict(0).Get2D();
                    if (isTakerTeam) {
                      backXBound =
                          clamp(ballPos.coords[0] + 50 * team->GetDynamicSide(),
                                -pitchHalfW, pitchHalfW);
                      frontXBound = clamp(
                          ballPos.coords[0] + 10 * -team->GetDynamicSide(),
                          -pitchHalfW, pitchHalfW);
                      xFocus = ballPos.coords[0];
                      xFocusStrength = 0.6;
                      yFocus = 0.0;
                      yFocusStrength = 0.8;
                    } else {
                      backXBound =
                          clamp(ballPos.coords[0] + 11 * team->GetDynamicSide(),
                                -pitchHalfW, pitchHalfW);
                      frontXBound = clamp(
                          ballPos.coords[0] + 20 * -team->GetDynamicSide(),
                          -pitchHalfW, pitchHalfW);
                      xFocus = ballPos.coords[0];
                      xFocusStrength = 1.0;
                      yFocus = 0.0;
                      yFocusStrength = 1.0;
                    }
                    lowYBound = -pitchHalfH * 0.8;
                    highYBound = pitchHalfH * 0.8;
                    Vector3 basePos = AdaptRestartFormation(
                        players[i], backXBound, frontXBound, lowYBound,
                        highYBound, xFocus, xFocusStrength, yFocus,
                        yFocusStrength, 0, 0, 0, 0, false);

                    // outside the box
                    signed int penaltySide =
                        (ball.Predict(0).coords[0] < 0) ? -1 : 1;
                    if (basePos.coords[0] * penaltySide >
                        pitchHalfW - 16.5 - 0.5)
                      basePos.coords[0] =
                          (pitchHalfW - 16.5 - 0.5) * penaltySide;

                    // outside penalty arc as well
                    if ((basePos - ball.Predict(0).Get2D())
                            .GetLength() < 9.15 + 0.5) {
                      basePos = ball.Predict(0).Get2D() +
                                (basePos - ball.Predict(0).Get2D())
                                        .GetNormalized() *
                                    (9.15 + 0.5);
                    }

                    players[i]->ResetPosition(
                        basePos, ball.Predict(0).Get2D());
                  }
                  break;

                default:
                  for (unsigned int i = 0; i < players.size(); i++) {
                    Vector3 basePos =
                        players[i]->GetFormationEntry().position *
                        Vector3(-team->GetDynamicSide() * pitchHalfW * 0.7,
                                -team->GetDynamicSide() * pitchHalfH * 0.7, 0);

                    players[i]->ResetPosition(
                        basePos, ball.Predict(0).Get2D());
                  }
                  break;
              }

              if (setPiece == e_GameMode_KickOff) {
                auto formation_players = team->GetAllPlayers();
                auto players_to_position = team->GetAllPlayers();
                if (regulation > football::sim::TickSpan{} &&
                    other_team->GetAllPlayers().size() ==
                        team->GetAllPlayers().size() &&
                    (options.left_team_owns_ball ^
                     kickoffTakerTeamId == 0)) {
                  formation_players = other_team->GetAllPlayers();
                }
                assert(formation_players.size() == players_to_position.size());
                for (int x = 0; x < formation_players.size(); x++) {
                  if (players_to_position[x]->IsActive()) {
                    Vector3 basePos =
                        formation_players[x]
                            ->GetFormationEntry()
                            .start_position *
                        Vector3(-team->GetDynamicSide() * pitchHalfW,
                                -team->GetDynamicSide() * pitchHalfH, 0);
                    players_to_position[x]->ResetPosition(
                        basePos, ball.Predict(0).Get2D());
                  }
                }
              }

              if (isTakerTeam) {
                auto ball_pos = ball.Predict(0).Get2D();
                std::vector<Player *> players;
                football::sim::query::GetClosestPlayers(team, ball.Predict(0).Get2D(),
                                     players, 2);
                taker = players[0];
                if (setPiece == e_GameMode_KickOff) {
                  // Do nothing
                } else if (setPiece == e_GameMode_ThrowIn) {
                  players[0]->ResetPosition(
                      ball_pos +
                          ball.Predict(0).Get2D().GetNormalized(
                              Vector3(0, -team->GetDynamicSide(), 0)) *
                              0.3f,
                      ball_pos);
                } else if (setPiece == e_GameMode_FreeKick) {
                  taker->ResetPosition(
                      ball_pos + Vector3(team->GetDynamicSide(), 0, 0) * 2.3f,
                      ball_pos);
                } else {
                  taker->ResetPosition(
                      ball_pos +
                          ball.Predict(0).Get2D().GetNormalized(
                              Vector3(0, -team->GetDynamicSide(), 0)) *
                              2.3f,
                      ball_pos);
                }
                if (setPiece == e_GameMode_ThrowIn) {
                  taker->SelectRetainAnim();
                  commands.SetBallRetainer(taker);
                }
                if (setPiece == e_GameMode_Penalty) {
                  taker->ResetPosition(
                      ball_pos + Vector3(team->GetDynamicSide(), 0, 0) * 3.0,
                      ball_pos);
                }

              } else
                taker = 0;
  return taker;
}
