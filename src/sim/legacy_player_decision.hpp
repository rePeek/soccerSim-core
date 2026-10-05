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

#ifndef _HPP_FOOTBALL_ONTHEPITCH_LEGACYPLAYERDECISION
#define _HPP_FOOTBALL_ONTHEPITCH_LEGACYPLAYERDECISION

#include "sim/player/humanoid/humanoid.hpp"

#include "sim/gamedefines.hpp"

class Match;
class Player;
class MentalImage;

// TRANSITIONAL SEAM, not the final architecture. This is the minimal surface the
// simulation needs from whoever decides a player's actions, so the dependency
// arrow can be flipped (sim -> abstract port, ai -> implementation) without
// changing behaviour. It deliberately mirrors the retired legacy controller
// concepts. The target design is
//
//   WorldState + TacticalBoard -> PlayerAI -> PlayerControlSet -> Simulation
//
// at which point the simulation should not know about decision objects at all.
// Do not grow this into a public API; add only what the simulation itself
// currently calls.
class LegacyPlayerDecision {

  public:
    LegacyPlayerDecision(Match *match) : match(match) { };
    virtual ~LegacyPlayerDecision() { };

    virtual void RequestCommand(PlayerCommandQueue &commandQueue) = 0;
    virtual void Process() { };
    virtual Vector3 GetDirection() = 0;
    virtual float GetFloatVelocity() = 0;
    virtual void SetPlayer(Player *player);

    // for convenience
    Player *GetPlayer() { return player; }
    Match *GetMatch() { return match; }

    virtual int GetReactionTime_ms();

    // Legacy execution/tactical cache. The decision owner chooses which
    // historical world snapshot its action planning reads; simulation only
    // consumes the resulting tactical ratings.
    virtual const MentalImage *GetMentalImage() = 0;

    virtual void Reset() = 0;

  protected:
    Player *player = 0;
    Match *match;
};

#endif
