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

#include "sim/observation/mentalimage.hpp"

#include "sim/ball/ball.hpp"
#include "sim/player/player.hpp"

MentalImage::MentalImage(football::sim::Tick captured_tick,
                         std::span<Player* const> allPlayers, const Ball& ball)
    : captured_tick(captured_tick) {
  players.resize(allPlayers.size());

  for (int playerCounter = 0; playerCounter < (signed int)allPlayers.size();
       playerCounter++) {

    Player *player = allPlayers[playerCounter];

    PlayerImage& playerImage = players[playerCounter];
    playerImage.player = player;
    playerImage.position = player->GetPosition();
    playerImage.directionVec = player->GetDirectionVec();
    playerImage.velocity = player->GetEnumVelocity();
    playerImage.movement = player->GetMovement();
    playerImage.role = player->GetDynamicFormationEntry().role;
  }

  UpdateBallPredictions(ball);
}

void MentalImage::Mirror(bool team_0, bool team_1, bool ball) {
  for (auto& i : players) {
    if (i.player->GetTeamID() == 0 ? team_0 : team_1) {
      i.Mirror();
    }
  }
  if (ball) {
    ballPredictions_mirrored = !ballPredictions_mirrored;
    for (auto& i : ballPredictions) {
      i.Mirror();
    }
  }
}

football::sim::TickSpan MentalImage::GetAge(football::sim::Tick now) const {
  return now - captured_tick;
}


PlayerImage MentalImage::GetPlayerImage(Player* p, football::sim::Tick now) const {
  for (auto& player : players) {
    if (player.player == p) {
      PlayerImage newImage = player;
      // Preserve multiplication order while replacing timestamp authority.
      Vector3 extrapolation = player.movement * football::sim::ToMilliseconds(GetAge(now)) * 0.001f;
      newImage.position = player.position + extrapolation;
      newImage.position = newImage.position.EnforceMaximumDeviation(newImage.player->GetPosition(), maxDistanceDeviation);
      newImage.movement = newImage.movement.EnforceMaximumDeviation(newImage.player->GetMovement(), maxMovementDeviation);
      return newImage;
    }
  }

  // failsafe
  return players[0];
}

std::vector<PlayerImagePosition> MentalImage::GetTeamPlayerImages(
    int teamID, football::sim::Tick now) const {
  std::vector<PlayerImagePosition> result;
  result.reserve(11);
  for (auto& player : players) {
    if (player.player->IsActive() && player.player->GetTeamID() == teamID) {
      Vector3 extrapolation = player.movement * football::sim::ToMilliseconds(GetAge(now)) * 0.001f;
      Vector3 position = player.position + extrapolation;
      position = position.EnforceMaximumDeviation(player.player->GetPosition(), maxDistanceDeviation);
      Vector3 movement = player.movement.EnforceMaximumDeviation(player.player->GetMovement(), maxMovementDeviation); // new
      result.emplace_back(position, movement, player.role);
    }
  }
  return result;
}

void MentalImage::UpdateBallPredictions(const Ball& ball) {
  ball.GetPredictionArray(ballPredictions);
}

Vector3 MentalImage::GetBallPrediction(int time_ms, football::sim::Tick now, const Ball& ball) const {
  // Preserve the old sampling adapter, including a horizon into the past.
  // This signed calculation input is not a simulation timestamp/deadline.
  if (time_ms < 0) {
    const auto backward_ms = static_cast<std::uint64_t>(-static_cast<std::int64_t>(time_ms));
    const auto age_ms = football::sim::ToMilliseconds(GetAge(now));
    const auto combined_ms = age_ms > backward_ms ? age_ms - backward_ms : 0;
    const auto index = std::min(combined_ms / football::sim::kMillisecondsPerTick,
        football::sim::ball_timing::kPredictionHorizon.value - 1);
    return ballPredictions[index].EnforceMaximumDeviation(
        ball.Predict(football::sim::TickSpan{}), maxDistanceDeviation);
  }
  return GetBallPrediction(football::sim::TickSpan{
      static_cast<std::uint64_t>(time_ms) / football::sim::kMillisecondsPerTick}, now, ball);
}

Vector3 MentalImage::GetBallPrediction(football::sim::TickSpan horizon,
                                      football::sim::Tick now, const Ball& ball) const {
  const auto last = football::sim::ball_timing::kPredictionHorizon - football::sim::TickSpan{1};
  // Clamp before adding, including arbitrary long manual timeline advances.
  const auto age = std::min(now - captured_tick, last);
  const auto index = std::min(horizon, last - age) + age;
  Vector3 mentalResult = ballPredictions[index.value];
  Vector3 realResult = ball.Predict(horizon);

  // let there be a maximum difference between the two. why?
  // when a ball gets a wholly new movement, this prediction is obviously far off reality, while some variables are not,
  // like the player->gettimeneededtogettoball, since that is based on non-delayed vars.
  // a solution would be to have a reaction-time-corrected version of everything, but that is, for now, too complicated.
  // maybe one day rebuild the whole timeneeded/tactics calculations system

  Vector3 result = mentalResult.EnforceMaximumDeviation(realResult, maxDistanceDeviation);

  return result;
}
