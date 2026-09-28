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

#include "match.hpp"

#include "../base/geometry/line.hpp"
#include <algorithm>
#include <cmath>

#include "../base/geometry/triangle.hpp"
#include "../base/log.hpp"
#include "../game_env.hpp"
#include "core/world/world_profiles.hpp"
#include "../main.hpp"
#include "AIsupport/AIfunctions.hpp"
#include "file.h"
#include "player/playerofficial.hpp"
#include "player/player_action_volume.hpp"
#include "player/player_body_collider.hpp"
#include "player/player_profile_adapter.hpp"


std::shared_ptr<AnimCollection> Match::GetAnimCollection() {
  DO_VALIDATION;
  return GetContext().anims;
}

const std::vector<Vector3> &Match::GetAnimPositionCache(Animation *anim) const {
  return GetContext().animPositionCache.find(anim)->second;
}

PlayerState& Match::GetTeamPlayerState(int team_id, int team_index) {
  assert(team_id >= 0 && team_id < 2);
  assert(team_index >= 0 && team_index < MAX_PLAYERS);
  return world_state.players[team_id * MAX_PLAYERS + team_index];
}

const football::domain::PlayerProfile& Match::GetTeamPlayerProfile(
    int team_id, int team_index) const {
  assert(team_id >= 0 && team_id < 2);
  assert(team_index >= 0 && team_index < MAX_PLAYERS);
  return profiles.players[team_id * MAX_PLAYERS + team_index];
}

Match::Match(std::unique_ptr<MatchData> match_data,
             const std::vector<AIControlledKeyboard *> &controllers,
             const MatchSetup& setup, bool animations,
             WorldState& world_state, WorldProfiles& profiles)
    : world_state(world_state), profiles(profiles),
      matchData(std::move(match_data)),
      first_team(GetScenarioConfig().reverse_team_processing ? 1 : 0),
      second_team(GetScenarioConfig().reverse_team_processing ? 0 : 1),
      controllers(controllers),
      controllerSetup(setup.controllers),
      possessionSideHistory(6000),
      matchDurationFactor(
          GetConfiguration()->GetReal("match_duration", 1.0) * 0.2f + 0.05f),
      _useMagnet(GetScenarioConfig().use_magnet) {
  DO_VALIDATION;
  auto& anims = GetContext().anims;
  GetContext().stablePlayerCount = 0;


  actualTime_ms = 0;
  goalScoredTimer = 0;


  ball = new BallLegacy(profiles.ball, world_state.ball, this);

  if (!anims) {
    DO_VALIDATION;
    anims = std::shared_ptr<AnimCollection>(new AnimCollection());
    anims->Load();
    // cache animation positions

    const std::vector < Animation* > &animationsTmp = anims->GetAnimations();
    for (unsigned int i = 0; i < animationsTmp.size(); i++) {
      DO_VALIDATION;
      std::vector<Vector3> positions;
      Animation *someAnim = animationsTmp[i];
      Quaternion dud;
      Vector3 position;
      for (int frame = 0; frame < someAnim->GetFrameCount(); frame++) {
        DO_VALIDATION;
        someAnim->GetKeyFrame(player, frame, dud, position);
        position.coords[2] = 0.0f;
        positions.push_back(position);
      }
      GetContext().animPositionCache.insert(std::pair < Animation*, std::vector<Vector3> >(someAnim, positions));
    }
  } else {
    for (auto& a : anims->GetAnimations()) {
      a->DirtyCache();
    }
  }
  designatedPossessionPlayer = 0;


  // teams

  assert(matchData != 0);
  // Compile legacy database attributes once, before any Player facade binds a
  // read-only profile view. Never copy profiles into WorldState predictions.
  // Profile holds inherent ability. Player::GetStat() is a different value: it
  // also applies AI difficulty and the current fatigue factor, which are match
  // context and condition rather than attributes of the player.
  profiles.players = {};
  for (int team_id = 0; team_id < 2; ++team_id) {
    const TeamData& data = matchData->GetTeamData(team_id);
    const int count = std::min(data.GetPlayerNum(), MAX_PLAYERS);
    for (int i = 0; i < count; ++i) {
      profiles.players[team_id * MAX_PLAYERS + i] =
          MakeSimulationPlayerProfile(*data.GetPlayerData(i));
    }
  }


  teams[first_team] =
      new Team(first_team, this, &matchData->GetTeamData(first_team),
               first_team ? GetScenarioConfig().right_team_difficulty
                          : GetScenarioConfig().left_team_difficulty);
  teams[second_team] =
      new Team(second_team, this, &matchData->GetTeamData(second_team),
               second_team ? GetScenarioConfig().right_team_difficulty
                           : GetScenarioConfig().left_team_difficulty);
  teams[first_team]->SetOpponent(teams[second_team]);
  teams[second_team]->SetOpponent(teams[first_team]);
  teams[first_team]->InitPlayers(anims);
  teams[second_team]->InitPlayers(anims);

  std::vector<Player*> activePlayers;
  teams[first_team]->GetActivePlayers(activePlayers);
  designatedPossessionPlayer = activePlayers.at(0);
  ballRetainer = 0;


  // officials

  officials = new Officials(this, anims);







  // 12th man sound

  // match params

  matchTime_ms = 0;
  lastGoalTeam = 0;
  for (unsigned int i = 0; i < e_TouchType_SIZE; i++) {
    DO_VALIDATION;
    lastTouchTeamIDs[i] = -1;
  }
  lastTouchTeamID = -1;
  lastGoalScorer = 0;
  bestPossessionTeam = 0;
  SetMatchPhase(e_MatchPhase_PreMatch);

  // everybody hates him, this poor bloke
  referee = new Referee(this, animations);



  lastBodyBallCollisionTime_ms = 0;

}

Match::~Match() { DO_VALIDATION; }

void Match::Mirror(bool team_0, bool team_1, bool ball) {
  GetTracker()->setDisabled(true);
  if (team_0) {
    teams[0]->Mirror();
  }
  if (team_1) {
    teams[1]->Mirror();
  }
  if (ball) {
    ball_mirrored = !ball_mirrored;
    this->ball->Mirror();
  }
  for (auto &i : mentalImages) {
    i.Mirror(team_0, team_1, ball);
  }
  GetTracker()->setDisabled(false);
}

void Match::Exit() {
  DO_VALIDATION;
  teams[first_team]->Exit();
  teams[second_team]->Exit();
  delete teams[first_team];
  delete teams[second_team];
  delete officials;
  delete ball;
  delete referee;
  mentalImages.clear();


}


void Match::UpdateControllerSetup() {
  DO_VALIDATION;
  const std::vector<ControllerSetup>& controller = controllerSetup;
  std::vector<AIControlledKeyboard*> left_players;
  std::vector<AIControlledKeyboard*> right_players;
  for (unsigned int i = 0; i < controller.size(); i++) {
    DO_VALIDATION;
    float mirror = 1.0;
    if (controller[i].side == -1) {
      left_players.push_back(controllers.at(controller[i].controller_id));
      DO_VALIDATION;
    } else if (controller[i].side == 1) {
      right_players.push_back(controllers.at(controller[i].controller_id));
      DO_VALIDATION;
      if (teams[1]->GetDynamicSide() == -1) {
        DO_VALIDATION;
        mirror = -1.0;
      }
    }
    controllers.at(controller[i].controller_id)->Mirror(mirror);
  }
  teams[0]->AddHumanGamers(left_players);
  teams[1]->AddHumanGamers(right_players);
}


void Match::GetActiveTeamPlayers(int teamID, std::vector<Player *> &players) {
  DO_VALIDATION;
  teams[teamID]->GetActivePlayers(players);
}

void Match::GetOfficialPlayers(std::vector<PlayerBase *> &players) {
  DO_VALIDATION;
  officials->GetPlayers(players);
}

MentalImage *Match::GetMentalImage(int history_ms) {
  DO_VALIDATION;
  int index = int(round((float)history_ms / 100.0));
  if (index >= (signed int)mentalImages.size()) index = mentalImages.size() - 1;
  if (index < 0) index = 0;
  return &mentalImages[index];
}

void Match::UpdateLatestMentalImageBallPredictions() {
  DO_VALIDATION;
  if (!mentalImages.empty()) mentalImages[0].UpdateBallPredictions();
}

void Match::ResetSituation(const Vector3 &focusPos) {
  DO_VALIDATION;
  SetBallRetainer(0);
  SetGoalScored(false);
  mentalImages.clear();
  goalScored = false;
  ballIsInGoal = false;
  for (unsigned int i = 0; i < e_TouchType_SIZE; i++) {
    DO_VALIDATION;
    lastTouchTeamIDs[i] = -1;
  }
  lastTouchTeamID = -1;
  lastGoalScorer = 0;
  bestPossessionTeam = 0;

  possessionSideHistory.Clear();

  lastBodyBallCollisionTime_ms = 0;

  ball->ResetSituation(focusPos);
  teams[first_team]->ResetSituation(focusPos);
  teams[second_team]->ResetSituation(focusPos);
  officials->GetReferee()->ResetSituation(focusPos);
}

void Match::SetMatchPhase(e_MatchPhase newMatchPhase) {
  matchPhase = newMatchPhase;
  teams[first_team]->RelaxFatigue(1.0f);
  teams[second_team]->RelaxFatigue(1.0f);
}

Team *Match::GetBestPossessionTeam() {
  DO_VALIDATION;
  return bestPossessionTeam;
}

void Match::ProcessState(EnvState* state) {
  if (state->getConfig()->reverse_team_processing) {
    std::swap(first_team, second_team);
  }
  state->process(first_team);
  state->process(second_team);
  if (state->getConfig()->reverse_team_processing) {
    std::swap(first_team, second_team);
  }
  bool team_0_mirror = teams[0]->isMirrored();
  bool team_1_mirror = teams[1]->isMirrored();
  bool ball_mirror =
      ball_mirrored ^ state->getConfig()->reverse_team_processing;
  Mirror(team_0_mirror, team_1_mirror, ball_mirror);
  std::vector<Player*> players;
  teams[first_team]->GetAllPlayers(players);
  teams[second_team]->GetAllPlayers(players);
  state->SetControllers(controllers);
  state->SetPlayers(players);
  state->SetAnimations(state->getContext()->anims->GetAnimations());
  state->SetTeams(teams[first_team], teams[second_team]);

  int size = mentalImages.size();
  state->process(size);
  mentalImages.resize(size);
  for (int x = 0; x < size; x++) {
    mentalImages[x].ProcessState(state, this);
  }
  teams[first_team]->ProcessState(state);
  teams[second_team]->ProcessState(state);
  std::vector<HumanGamer*> humanControllers;
  teams[first_team]->GetHumanControllers(humanControllers);
  teams[second_team]->GetHumanControllers(humanControllers);
  state->SetHumanControllers(humanControllers);
  for (auto &player : players) {
    player->ProcessState(state);
  }
  matchData->ProcessState(state, first_team);
  officials->ProcessState(state);
  {
    std::vector<HumanGamer*> human_gamers;
    std::set<AIControlledKeyboard*> visited;
    teams[first_team]->GetHumanControllers(human_gamers);
    teams[second_team]->GetHumanControllers(human_gamers);
    for (auto& c : human_gamers) {
      c->GetHIDevice()->ProcessState(state);
      visited.insert(c->GetHIDevice());
    }
    for (auto& c : controllers) {
      if (!visited.count(c)) {
        c->ProcessState(state);
      }
    }
  }
  ball->ProcessState(state);
  state->process(matchTime_ms);
  state->process(actualTime_ms);
  state->process(goalScoredTimer);
  state->process(matchPhase);
  state->process(inPlay);
  state->process(inSetPiece);
  state->process(goalScored);
  state->process(ballIsInGoal);
  state->process(lastGoalTeam);
  state->process(lastGoalScorer);
  if (first_team == 1) {
    for (int &v : lastTouchTeamIDs) {
      if (v != -1) {
        v = 1 - v;
      }
    }
  }
  for (int& v : lastTouchTeamIDs) {
    state->process(v);
  }
  if (first_team == 1) {
    for (int &v : lastTouchTeamIDs) {
      if (v != -1) {
        v = 1 - v;
      }
    }
  }
  if (first_team == 1 && lastTouchTeamID != -1) {
    lastTouchTeamID = 1 - lastTouchTeamID;
  }
  state->process(lastTouchTeamID);
  if (first_team == 1 && lastTouchTeamID != -1) {
    lastTouchTeamID = 1 - lastTouchTeamID;
  }
  state->process(bestPossessionTeam);
  state->process(designatedPossessionPlayer);
  state->process(ballRetainer);
  possessionSideHistory.ProcessState(state);
  state->process(lastBodyBallCollisionTime_ms);
  referee->ProcessState(state);

  Mirror(team_0_mirror, team_1_mirror, ball_mirror);
}

void Match::GetTeamState(SharedInfo *state,
                         std::map<AIControlledKeyboard *, int> &controller_mapping,
                         int team_id) {
  DO_VALIDATION;
  std::vector<PlayerInfo> &team =
      team_id == 0 ? state->left_team : state->right_team;
  team.clear();
  std::vector<Player *> players;
  teams[team_id]->GetAllPlayers(players);
  auto main_player = teams[team_id]->MainSelectedPlayer();
  for (auto player : players) {
    DO_VALIDATION;
    auto controller = player->ExternalController();
    if (controller) {
      DO_VALIDATION;
      if (team_id == 0) {
        state->left_controllers[controller_mapping[controller->GetHIDevice()]]
            .controlled_player = team.size();
      } else {
        state->right_controllers[controller_mapping[controller->GetHIDevice()]]
            .controlled_player = team.size();
      }
    }
    if (player->CastHumanoid() != NULL) {
      DO_VALIDATION;
      auto position = player->GetPosition();
      auto movement = player->GetMovement();
      if (team_id == 1) {
        position.Mirror();
        movement.Mirror();
      }
      PlayerInfo info;
      info.player_position = position.coords;
      info.player_direction =
          (movement / GetGameConfig().physics_steps_per_frame).coords;
      info.tired_factor = 1 - player->GetFatigueFactorInv();
      info.has_card = player->HasCards();
      info.is_active = player->IsActive();
      info.role = player->GetFormationEntry().role;
      if (player->HasPossession() && GetLastTouchTeamID() != -1 &&
          GetLastTouchTeam()->GetLastTouchPlayer() == player) {
        DO_VALIDATION;
        state->ball_owned_player = team.size();
        state->ball_owned_team = GetLastTouchTeamID();
      }
      info.designated_player = player == main_player;
      team.push_back(info);
    }
  }
}

void Match::GetState(SharedInfo *state) {
  DO_VALIDATION;
  state->ball_position = ball->GetAveragePosition(5).coords;
  state->ball_rotation =
      (ball->GetRotation() / GetGameConfig().physics_steps_per_frame).coords;
  state->ball_direction =
      (ball->GetMovement() / GetGameConfig().physics_steps_per_frame).coords;
  state->ball_owned_player = -1;
  state->ball_owned_team = -1;
  state->left_goals = GetScore(0);
  state->right_goals = GetScore(1);
  // Report a step before game starts as in play, so that we know which players
  // are controlled by agents and which are controlled using action_builtin_ai.
  // 1900 = 2000 (game start) - 100 (single step time).
  state->is_in_play = IsInPlay() || GetActualTime_ms() == 1900;
  state->game_mode = IsInSetPiece() ? referee->GetBuffer().desiredSetPiece : e_GameMode_Normal;
  state->left_controllers.clear();
  state->left_controllers.resize(GetScenarioConfig().left_team.size());
  state->right_controllers.clear();
  state->right_controllers.resize(GetScenarioConfig().right_team.size());

  std::map<AIControlledKeyboard*, int> controller_mapping;
  {
    auto controllers = GetControllers();
    CHECK(controllers.size() == 2 * MAX_PLAYERS);
    for (int x = 0; x < MAX_PLAYERS; x++) {
      DO_VALIDATION;
      controller_mapping[controllers[x]] = x;
      controller_mapping[controllers[x + MAX_PLAYERS]] = x;
    }
  }
  GetTeamState(state, controller_mapping, first_team);
  GetTeamState(state, controller_mapping, second_team);
}

// THE SPICE

bool Match::Process() {
  DO_VALIDATION;
  bool reverse = GetScenarioConfig().reverse_team_processing;
  DO_VALIDATION;


  Mirror(reverse, !reverse, reverse);
  if (IsInPlay()) {
    DO_VALIDATION;
    CheckBallCollisions();
  }

  // HIJ IS EEN HONDELUUUL
  referee->Process();
  DO_VALIDATION;
  Vector3 previousBallPos = ball->Predict(0);
  Mirror(reverse, !reverse, reverse);
  if (!IsInPlay() && referee->GetBuffer().prepareTime + 10 < GetActualTime_ms()) {
    // Do not do simulation when game is on hold to save CPU.
    BumpActualTime_ms(10);
    return false;
  }
  Mirror(false, false, reverse);
  ball->Process();
  Mirror(false, false, reverse);

  // create mental images for the AI to use
  if (mentalImages.empty() || GetActualTime_ms() % 100 == 0) {
    DO_VALIDATION;
    mentalImages.insert(mentalImages.begin(), MentalImage(this));
    if (mentalImages.size() > 3) {
      DO_VALIDATION;
      mentalImages.pop_back();
    }
  }

  // obvious
  teams[first_team]->UpdateSwitch();
  teams[second_team]->UpdateSwitch();

  Mirror(first_team == 1, first_team == 0, first_team == 1);
  teams[first_team]->Process();
  Mirror(true, true, true);
  teams[second_team]->Process();
  Mirror(first_team == 0, first_team == 1, first_team == 0);

  Mirror(reverse, !reverse, reverse);
  officials->Process();
  Mirror(reverse, !reverse, reverse);

  Mirror(first_team == 1, first_team == 0, first_team == 1);
  teams[first_team]->UpdatePossessionStats();
  Mirror(true, true, true);
  teams[second_team]->UpdatePossessionStats();
  Mirror(first_team == 0, first_team == 1, first_team == 0);

  CalculateBestPossessionTeamID();

  if (GetBallRetainer() == 0) {
    DO_VALIDATION;
    if (GetBestPossessionTeam()) {
      DO_VALIDATION;
      Player *candidate = GetBestPossessionTeam()->GetDesignatedTeamPossessionPlayer();
      if (candidate != GetDesignatedPossessionPlayer()) {
        DO_VALIDATION;
        unsigned int designatedTime = GetDesignatedPossessionPlayer()->GetTimeNeededToGetToBall_ms();
        unsigned int candidateTime = candidate->GetTimeNeededToGetToBall_ms();
        float timeRating = (float)(candidateTime + 10) / (float)(designatedTime + 10);
        if (timeRating < 0.85f) designatedPossessionPlayer = candidate;
      }
    } else {
      // just stick with current team
      designatedPossessionPlayer = GetDesignatedPossessionPlayer()->GetTeam()->GetDesignatedTeamPossessionPlayer();
    }
  } else {
    designatedPossessionPlayer = GetBallRetainer();
  }

  Mirror(reverse, !reverse, reverse);
  CheckHumanoidCollisions();

  BumpActualTime_ms(10);

  // check for goals
  bool first_team_goal = false;
  bool second_team_goal = false;
  if (IsInPlay()) {
    first_team_goal =
        CheckForGoal(teams[first_team]->GetDynamicSide(), previousBallPos);
    second_team_goal =
        CheckForGoal(teams[second_team]->GetDynamicSide(), previousBallPos);
  }
  bool goal = first_team_goal | second_team_goal;
  ballIsInGoal |= goal;
  Mirror(reverse, !reverse, reverse);
  if (IsInPlay()) {
    DO_VALIDATION;
    if (goal) {
      int team = first_team_goal ? second_team : first_team;
      DO_VALIDATION;
      matchData->SetGoalCount(teams[team]->GetID(),
                              matchData->GetGoalCount(team) + 1);
      goalScored = true;
      lastGoalTeam = teams[team];
      teams[team]->GetController()->UpdateTactics();
    }
    if (first_team_goal || second_team_goal) {
      DO_VALIDATION;

      // find out who scored
      bool ownGoal = true;
      if (GetLastTouchTeamID(e_TouchType_Intentional_Kicked) == GetLastGoalTeam()->GetID() || GetLastTouchTeamID(e_TouchType_Intentional_Nonkicked) == GetLastGoalTeam()->GetID()) ownGoal = false;

      if (!ownGoal) {
        DO_VALIDATION;
        lastGoalScorer = GetLastGoalTeam()->GetLastTouchPlayer();
      }

      else {  // own goal
        lastGoalScorer = teams[abs(GetLastGoalTeam()->GetID() - 1)]->GetLastTouchPlayer();
      }
    }
  }
  // average possession side

   if (IsInPlay()) {
     DO_VALIDATION;
     if (GetBestPossessionTeam()) {
       DO_VALIDATION;
       float sideValue = 0;
       sideValue += (GetTeam(0)->GetFadingTeamPossessionAmount() - 0.5f) *
           GetTeam(0)->GetDynamicSide();
       sideValue += (GetTeam(1)->GetFadingTeamPossessionAmount() - 0.5f) *
           GetTeam(1)->GetDynamicSide();
       possessionSideHistory.Insert(sideValue);
     }
   }

   if (GetReferee()->GetBuffer().active == true &&
       (GetReferee()->GetCurrentFoulType() == 2 ||
           GetReferee()->GetCurrentFoulType() == 3) &&
           GetReferee()->GetBuffer().stopTime < GetActualTime_ms() - 1000) {
     DO_VALIDATION;

     if (GetReferee()->GetBuffer().prepareTime > GetActualTime_ms()) {
       DO_VALIDATION;  // FOUL, film referee
       if (officials->GetReferee()->GetSimulationActionState().type == e_FunctionType_Special) referee->AlterSetPiecePrepareTime(GetActualTime_ms() + 1000);
     }
  }
  return true;
}

bool Match::CheckForGoal(signed int side, const Vector3 &previousBallPos) {
  DO_VALIDATION;
  if (fabs(ball->Predict(10).coords[0]) < pitchHalfW - 1.0) return false;

  Line line;
  line.SetVertex(0, previousBallPos);
  line.SetVertex(1, ball->Predict(0));

  Triangle goal1;
  goal1.SetVertex(0, Vector3((pitchHalfW + lineHalfW + 0.11f) * side, 3.7f, 0));
  goal1.SetVertex(1, Vector3((pitchHalfW + lineHalfW + 0.11f) * side, -3.7f, 0));
  goal1.SetVertex(2, Vector3((pitchHalfW + lineHalfW + 0.11f) * side, 3.7f, 2.5f));
  goal1.SetNormals(Vector3(-side, 0, 0));
  Triangle goal2;
  goal2.SetVertex(0, Vector3((pitchHalfW + lineHalfW + 0.11f) * side, -3.7f, 0));
  goal2.SetVertex(1, Vector3((pitchHalfW + lineHalfW + 0.11f) * side, -3.7f, 2.5f));
  goal2.SetVertex(2, Vector3((pitchHalfW + lineHalfW + 0.11f) * side, 3.7f, 2.5f));
  goal2.SetNormals(Vector3(-side, 0, 0));

  Vector3 intersectVec;
  GetTracker()->setDisabled(true);
  bool intersect1 = goal1.IntersectsLine(line, intersectVec);
  bool intersect2 = goal2.IntersectsLine(line, intersectVec);
  GetTracker()->setDisabled(false);
  // extra check: ball could have gone 'in' via the side netting, if line begin
  // == inside pitch, but outside of post, and line end == in goal. disallow!
  if (fabs(previousBallPos.coords[1]) > 3.7 &&
      fabs(previousBallPos.coords[0]) > pitchHalfW - lineHalfW - 0.11) {
    DO_VALIDATION;
    return false;
  }
  if (intersect1 || intersect2) {
    DO_VALIDATION;
    return true;
  }
  return false;
}

void Match::CalculateBestPossessionTeamID() {
  DO_VALIDATION;
  if (GetBallRetainer() != 0) {
    DO_VALIDATION;
    bestPossessionTeam = GetBallRetainer()->GetTeam();
  } else {
    int bestTime_ms[2] = { 100000, 100000 };
    bestTime_ms[0] = teams[first_team]->GetTimeNeededToGetToBall_ms();
    bestTime_ms[1] = teams[second_team]->GetTimeNeededToGetToBall_ms();
    if (bestTime_ms[0] < bestTime_ms[1])
      bestPossessionTeam = teams[first_team];
    else if (bestTime_ms[0] > bestTime_ms[1])
      bestPossessionTeam = teams[second_team];
    else {
      assert(bestTime_ms[0] == bestTime_ms[1]);
      bestPossessionTeam = 0;
    }
  }
}

void Match::CheckHumanoidCollisions() {
  DO_VALIDATION;
  std::vector<Player*> players;

  GetTeam(first_team)->GetActivePlayers(players);
  GetTeam(second_team)->GetActivePlayers(players);

  // outer vectors index == players[] index
  std::vector < std::vector<PlayerBounce> > playerBounces;

  // insert an empty entry for every player
  playerBounces.resize(players.size());

  // check each combination of humanoids once
  for (unsigned int i1 = 0; i1 < players.size() - 1; i1++) {
    DO_VALIDATION;
    for (unsigned int i2 = i1 + 1; i2 < players.size(); i2++) {
      DO_VALIDATION;
      CheckHumanoidCollision(players.at(i1), players.at(i2), playerBounces.at(i1), playerBounces.at(i2));
    }
  }

  // do bouncy magic
  for (unsigned int i1 = 0; i1 < players.size(); i1++) {
    DO_VALIDATION;

    float totalForce = 0.0f;

    for (unsigned int i2 = 0; i2 < playerBounces.at(i1).size(); i2++) {
      DO_VALIDATION;

      const PlayerBounce &bounce = playerBounces.at(i1).at(i2);
      totalForce += bounce.force;
    }

    if (totalForce > 0.0f) {
      DO_VALIDATION;

      Vector3 bounceVec;
      for (unsigned int i2 = 0; i2 < playerBounces.at(i1).size(); i2++) {
        DO_VALIDATION;

        const PlayerBounce &bounce = playerBounces.at(i1).at(i2);
        bounceVec += (bounce.opp->GetMovement() - players.at(i1)->GetMovement()) * bounce.force * (bounce.force / totalForce);
      }

      // okay, accumulated all, now distribute them in normalized fashion
      players.at(i1)->OffsetPosition(bounceVec * 0.01f * 1.0f);
    }
  }
}

void Match::CheckHumanoidCollision(Player *p1, Player *p2,
                                   std::vector<PlayerBounce> &p1Bounce,
                                   std::vector<PlayerBounce> &p2Bounce) {
  DO_VALIDATION;
  constexpr float distanceFactor = 0.72f;
  constexpr float bouncePlayerRadius = 0.5f * distanceFactor;
  constexpr float similarPlayerRadius = 0.8f * distanceFactor;
  constexpr float similarExp = 0.2f;//0.8f;
  constexpr float similarForceFactor = 0.25f; // 0.5f would be the full effect

  // Derived colliders (Profile + State). The serialized shadow is not read here.
  const football::contact::CircleCollider p1Collider =
      p1->GetDerivedGroundCollider();
  const football::contact::CircleCollider p2Collider =
      p2->GetDerivedGroundCollider();
  Vector3 p1pos = p1Collider.center;
  Vector3 p2pos = p2Collider.center;
  float distance = (p1pos - p2pos).GetLength();

  Vector3 p1movement = p1->GetKinematicState().velocity;
  Vector3 p2movement = p2->GetKinematicState().velocity;
  assert(p1movement.coords[2] == 0.0f);
  assert(p2movement.coords[2] == 0.0f);

  float bounceBias = 0.0f;
  Vector3 bounceVec;
  float p1backFacing = 0.5f;
  float p2backFacing = 0.5f;

  if (distance < bouncePlayerRadius * 2.0f ||
      distance < (bouncePlayerRadius + similarPlayerRadius) * 2.0f) {
    DO_VALIDATION;

    bounceVec = (p1pos - p2pos).GetNormalized(Vector3(0, -1, 0));

    // back facing
    Vector3 p1facing = p1->GetKinematicState().movementFacing.GetRotated2D(p1->GetRelBodyAngle() * 0.7f);
    Vector3 p2facing = p2->GetKinematicState().movementFacing.GetRotated2D(p2->GetRelBodyAngle() * 0.7f);
    p1backFacing = clamp(p1facing.GetDotProduct( bounceVec) * 0.5f + 0.5f, 0.0f, 1.0f); // 0 .. 1 == worst .. best
    p2backFacing = clamp(p2facing.GetDotProduct(-bounceVec) * 0.5f + 0.5f, 0.0f, 1.0f);

    if (p1Collider.Intersects(p2Collider)) {
      DO_VALIDATION;

      bounceBias += p1backFacing * 0.8f;
      bounceBias -= p2backFacing * 0.8f;

      // velocity, faster is worse
      float p1velocity = p1->GetFloatVelocity();
      float p2velocity = p2->GetFloatVelocity();
      const PlayerActionState &p1Action = p1->GetSimulationActionState();
      const PlayerActionState &p2Action = p2->GetSimulationActionState();
      bounceBias -= clamp(((p1velocity - p2velocity) / sprintVelocity) * 0.2f, -0.2f, 0.2f);

      if (p1Action.IsContactPending() && p1Action.type == e_FunctionType_Interfere) bounceBias += 0.1f + 0.4f * p1->GetStat(technical_standingtackle);
      if (p1Action.IsContactPending() && p1Action.type == e_FunctionType_Sliding)   bounceBias += 0.1f + 0.4f * p1->GetStat(technical_slidingtackle);
      if (p2Action.IsContactPending() && p2Action.type == e_FunctionType_Interfere) bounceBias -= 0.1f + 0.4f * p2->GetStat(technical_standingtackle);
      if (p2Action.IsContactPending() && p2Action.type == e_FunctionType_Sliding)   bounceBias -= 0.1f + 0.4f * p2->GetStat(technical_slidingtackle);

      // problem is, once possession is lost (usually directly after ball is touched), bias may turn around the other way. (well, maybe that's not a problem. dunno.)
      // if (p1->HasPossession() == true) bounceBias -= 0.3f;
      // if (p2->HasPossession() == true) bounceBias += 0.3f;

      if (p1 == GetDesignatedPossessionPlayer()) bounceBias += 0.4f;
      if (p2 == GetDesignatedPossessionPlayer()) bounceBias -= 0.4f;

      // closest to ball
      if (p1 == p1->GetTeam()->GetDesignatedTeamPossessionPlayer() &&
          p2 == p2->GetTeam()->GetDesignatedTeamPossessionPlayer()) {
        DO_VALIDATION;
        float p1BallDistance = (GetBall()->Predict(10).Get2D() - p1->GetPosition()).GetLength();
        float p2BallDistance = (GetBall()->Predict(10).Get2D() - p2->GetPosition()).GetLength();
        float ballDistanceDiffFactor = clamp(std::min(p2BallDistance, 1.2f) - std::min(p1BallDistance, 1.2f), -0.6f, 0.6f) * 1.0f; // std::min is cap so difference won't matter if ball is far away (so only used in battles about the ball)
        bounceBias += ballDistanceDiffFactor;
      }

      bounceBias += p1->GetStat(physical_balance) * 1.0f;
      bounceBias -= p2->GetStat(physical_balance) * 1.0f;

      bounceBias = clamp(bounceBias, -1.0f, 1.0f);
      bounceBias *= 0.5f;

      // convert bounceBias to 0 .. 1 instead of -1 .. 1
      float bounceBias0to1 = bounceBias * 0.5f + 0.5f;
      //bounceBias0to1 = curve(bounceBias0to1, 0.5f); // more binary

      Vector3 offset1 = (p1pos - p2pos).GetNormalized(0) * (bouncePlayerRadius - distance * 0.5f) * (1.0f - bounceBias0to1) * 2.0f;
      Vector3 offset2 = (p2pos - p1pos).GetNormalized(0) * (bouncePlayerRadius - distance * 0.5f) * bounceBias0to1 * 2.0f;

      // slow down on contact
      /*
      Vector3 averageMomentum = (p1movement + p2movement) * 0.5f;
      offset1 -= averageMomentum * 0.001f;
      offset2 -= averageMomentum * 0.001f;
      */

      // make players snap to the side of opponents (rather, just a bit in front of them too)


      if (GetDesignatedPossessionPlayer() == p2 && p2->HasPossession()) {
        DO_VALIDATION;
        Vector3 p2_leftside = p2pos + p2->GetKinematicState().movementFacing.GetRotated2D(0.3f * pi) * bouncePlayerRadius * 2;
        Vector3 p2_rightside = p2pos + p2->GetKinematicState().movementFacing.GetRotated2D(-0.3f * pi) * bouncePlayerRadius * 2;
        float p1_to_p2_left = (p1pos - p2_leftside).GetLength();
        float p1_to_p2_right = (p1pos - p2_rightside).GetLength();
        Vector3 p2side = p1_to_p2_left < p1_to_p2_right ? p2_leftside : p2_rightside;
        // SetYellowDebugPilon(p2side);
        offset1 += (p2side - p1pos).GetNormalizedMax(0.01f) * p1->GetStat(physical_balance) * 0.3f;
      }

      else if (GetDesignatedPossessionPlayer() == p1 && p1->HasPossession()) {
        DO_VALIDATION;
        Vector3 p1_leftside = p1pos + p1->GetKinematicState().movementFacing.GetRotated2D(0.3f * pi) * bouncePlayerRadius * 2;
        Vector3 p1_rightside = p1pos + p1->GetKinematicState().movementFacing.GetRotated2D(-0.3f * pi) * bouncePlayerRadius * 2;
        float p2_to_p1_left = (p2pos - p1_leftside).GetLength();
        float p2_to_p1_right = (p2pos - p1_rightside).GetLength();
        Vector3 p1side = p2_to_p1_left < p2_to_p1_right ? p1_leftside : p1_rightside;
        // SetRedDebugPilon(p1side);
        offset2 += (p1side - p2pos).GetNormalizedMax(0.01f) * p2->GetStat(physical_balance) * 0.3f;
      }

      // can not bump faster than sprint
      offset1.NormalizeMax(sprintVelocity * 0.01f);
      offset2.NormalizeMax(sprintVelocity * 0.01f);


      p1->OffsetPosition(offset1);
      p2->OffsetPosition(offset2);
    }

    // take over each others movement a bit (precalc phase)

    float similarBias = 0.0f;

    if (similarForceFactor > 0.0f &&
        distance < (bouncePlayerRadius + similarPlayerRadius) * 2.0f) {
      DO_VALIDATION;
      float shellDistance = std::max(0.0f, distance - bouncePlayerRadius * 2.0f);

      similarBias += p1backFacing * 0.8f;
      similarBias -= p2backFacing * 0.8f;

      // velocity, faster is worse
      float p1velocity = p1->GetFloatVelocity();
      float p2velocity = p2->GetFloatVelocity();
      similarBias -= clamp(((p1velocity - p2velocity) / sprintVelocity) * 0.2f, -0.2f, 0.2f);

      if (p1 == GetDesignatedPossessionPlayer()) similarBias += 0.6f;
      if (p2 == GetDesignatedPossessionPlayer()) similarBias -= 0.6f;

      // closest to ball
      if (p1 == p1->GetTeam()->GetDesignatedTeamPossessionPlayer() &&
          p2 == p2->GetTeam()->GetDesignatedTeamPossessionPlayer()) {
        DO_VALIDATION;
        float p1BallDistance = (GetBall()->Predict(10).Get2D() - p1->GetPosition()).GetLength();
        float p2BallDistance = (GetBall()->Predict(10).Get2D() - p2->GetPosition()).GetLength();
        float ballDistanceDiffFactor = clamp(std::min(p2BallDistance, 1.2f) - std::min(p1BallDistance, 1.2f), -0.6f, 0.6f) * 1.0f; // std::min is cap so difference won't matter if ball is far away (so only used in battles about the ball)
        similarBias += ballDistanceDiffFactor;
      }

      similarBias += p1->GetStat(physical_balance) * 1.0f;
      similarBias -= p2->GetStat(physical_balance) * 1.0f;

      similarBias = clamp(similarBias, -1.0f, 1.0f);
      similarBias *= 0.9f;

      float similarForce = clamp(1.0f - (shellDistance / (similarPlayerRadius * 2.0f)), 0.0f, 1.0f);
      similarForce = std::pow(similarForce, similarExp);
      similarForce *= similarForceFactor;

      assert(similarForce >= 0.0f && similarForce <= 1.0f);

      float similarBias0to1 = similarBias * 0.5f + 0.5f;


      PlayerBounce player1Bounce;
      player1Bounce.opp = p2;
      player1Bounce.force = similarForce * (1.0f - similarBias0to1);
      p1Bounce.push_back(player1Bounce);

      PlayerBounce player2Bounce;
      player2Bounce.opp = p1;
      player2Bounce.force = similarForce * similarBias0to1;
      p2Bounce.push_back(player2Bounce);
    }

    // u b trippin?

    if (distance < bouncePlayerRadius * 2.0f) {
      DO_VALIDATION;

      float p1sensitivity = 0.0f;
      float p2sensitivity = 0.0f;

      p1sensitivity += (1.0f - p1backFacing) * 1.0f;
      p2sensitivity += (1.0f - p2backFacing) * 1.0f;

      // velocity, faster is worse
      float p1velocity = p1->GetFloatVelocity();
      float p2velocity = p2->GetFloatVelocity();
      p1sensitivity += NormalizedClamp(p1velocity, idleVelocity, sprintVelocity) * 1.0f;
      p2sensitivity += NormalizedClamp(p2velocity, idleVelocity, sprintVelocity) * 1.0f;

      if (p1->HasBestPossession() == true) p1sensitivity += 1.0f;
      if (p2->HasBestPossession() == true) p2sensitivity += 1.0f;

      float balanceWeight = 3.0f;
      p1sensitivity += (1.0f - p1->GetStat(physical_balance) * 1.0f) * balanceWeight;
      p2sensitivity += (1.0f - p2->GetStat(physical_balance) * 1.0f) * balanceWeight;

      p1sensitivity += clamp(p1->GetDecayingPositionOffsetLength() * 10.0f, 0.0f, 1.0f);
      p2sensitivity += clamp(p2->GetDecayingPositionOffsetLength() * 10.0f, 0.0f, 1.0f);

      // penetration
      float penetrationWeight = 6.0f;
      float penetration = ( (p1->GetPosition() + p1->GetKinematicState().velocity * 0.03f) - (p2->GetPosition() + p2->GetKinematicState().velocity * 0.03f) ).GetLength();
      //if (p1->GetDebug() || p2->GetDebug()) printf("penetration: %f\n", pow(1.0f - NormalizedClamp(penetration, 0.0f, bouncePlayerRadius * 2.0f), 0.4f));
      p1sensitivity +=
          std::pow(1.0f - NormalizedClamp(penetration, 0.0f,
                                          bouncePlayerRadius * 2.0f),
                   0.4f) *
          penetrationWeight;
      p2sensitivity +=
          std::pow(1.0f - NormalizedClamp(penetration, 0.0f,
                                          bouncePlayerRadius * 2.0f),
                   0.4f) *
          penetrationWeight;

      // ball proximity (usually means: stability is less because we sacrifice balance to control the ball)
      float p1BallDistance = (GetBall()->Predict(10).Get2D() - p1->GetPosition()).GetLength();
      float p2BallDistance = (GetBall()->Predict(10).Get2D() - p2->GetPosition()).GetLength();
      p1sensitivity += 1.0f - NormalizedClamp(p1BallDistance, 0.0f, 0.7f);
      p2sensitivity += 1.0f - NormalizedClamp(p2BallDistance, 0.0f, 0.7f);

      // divided by elements active
      p1sensitivity /= 5.0f + balanceWeight + penetrationWeight;
      p2sensitivity /= 5.0f + balanceWeight + penetrationWeight;

      float trip0threshold = 0.38f;
      float trip1threshold = 0.48f;
      float trip2threshold = 0.58f;

      if (p1sensitivity > trip0threshold) {
        DO_VALIDATION;
        int tripType = 0;
        if (p1sensitivity > trip1threshold) tripType = 1;
        if (p1sensitivity > trip2threshold) tripType = 2;
        if (tripType > 0) {
          DO_VALIDATION;
          p1->TripMe((p1->GetKinematicState().velocity * 0.1f + p2->GetKinematicState().velocity * 0.06f + bounceVec * 1.0f).GetNormalized(bounceVec), tripType);
          referee->TripNotice(p1, p2, tripType);
        }
      }
      if (p2sensitivity > trip0threshold) {
        DO_VALIDATION;
        int tripType = 0;
        if (p2sensitivity > trip1threshold) tripType = 1;
        if (p2sensitivity > trip2threshold) tripType = 2;
        if (tripType > 0) {
          DO_VALIDATION;
          p2->TripMe((p2->GetKinematicState().velocity * 0.1f + p1->GetKinematicState().velocity * 0.06f - bounceVec * 1.0f).GetNormalized(-bounceVec), tripType);
          referee->TripNotice(p2, p1, tripType);
        }
      }

    }  // within either bump, similar or trip range
  }

  // check for tackling collisions

  // The tackle contact test is a simulation-owned action volume (a forward
  // capsule) against the victim's ground collider. It replaces the legacy
  // per-body-part geometry AABB test, so no scene object is involved.
  const PlayerActionState &p1Action = p1->GetSimulationActionState();
  const PlayerActionState &p2Action = p2->GetSimulationActionState();
  const PlayerActionVolume p1Tackle =
      BuildTackleVolume(p1Action, p1->GetKinematicState());
  const PlayerActionVolume p2Tackle =
      BuildTackleVolume(p2Action, p2->GetKinematicState());
  int tackle = 0;
  if (p1Tackle.active) tackle += 1;
  if (p2Tackle.active) tackle += 2;
  if (distance < 2.0f && tackle > 0 && tackle < 3) {
    DO_VALIDATION;  // if tackle is 3, ignore both
    const PlayerActionVolume &tacklerVolume =
        tackle == 1 ? p1Tackle : p2Tackle;
    const PlayerActionState &tacklerAction =
        tackle == 1 ? p1Action : p2Action;
    Player *tackler = tackle == 1 ? p1 : p2;
    Player *victim = tackle == 1 ? p2 : p1;

    // Derived, like the movement state: Profile + State, never the shadow.
    const football::contact::CircleCollider victimCollider =
        victim->GetDerivedGroundCollider();
    if (tacklerVolume.Intersects(victimCollider)) {
      DO_VALIDATION;
      if (tacklerAction.frame > 10 &&
          tacklerAction.frame < tacklerAction.frameCount - 6) {
        DO_VALIDATION;
        Vector3 tripVec = victim->GetKinematicState().movementFacing;
        int tripType = 3;  // sliding
        if (tacklerAction.type == e_FunctionType_Interfere)
          tripType = 1;  // was 2
        victim->TripMe(tripVec, tripType);
        referee->TripNotice(victim, tackler, tripType);
      }
    }
  }
}

void Match::CheckBallCollisions() {
  DO_VALIDATION;



  //printf("%i - %i hihi\n", actualTime_ms, lastBodyBallCollisionTime_ms + 150);
  if (actualTime_ms <= lastBodyBallCollisionTime_ms + 150) return;

  std::vector<Player*> players;
  GetTeam(first_team)->GetActivePlayers(players);
  GetTeam(second_team)->GetActivePlayers(players);

  Vector3 bounceVec;
  float bias = 0.0;
  int bounceCount = 0; // this shit is shit, average properly in combination with bias or something like that

  //printf("lasttouchbias: %f, isnul?: %s\n", GetLastTouchBias(200), GetLastTouchBias(200) == 0.0f ? "true" : "false");
  for (int i = 0; i < (signed int)players.size(); i++) {
    DO_VALIDATION;
    const PlayerActionState &action = players[i]->GetSimulationActionState();

    bool biggestRatio = false;
    int teamID = players[i]->GetTeam()->GetID();

    int touchTimeThreshold_ms = 200;//700;
    float oppLastTouchBias = GetTeam(abs(teamID - 1))->GetLastTouchBias(touchTimeThreshold_ms);
    float lastTouchBias = players[i]->GetLastTouchBias(touchTimeThreshold_ms);
    float oppLastTouchBiasLong = GetTeam(abs(teamID - 1))->GetLastTouchBias(1600);

    if (lastTouchBias <= 0.01f &&
        oppLastTouchBias > 0.01f /* && ballTowardsPlayer*/) {
      DO_VALIDATION;  // cannot collide if opp didn't recently touch ball (we
                      // would be able to predict ball by then), or if player
                      // itself already did (to overcome the 'perpetuum
                      // collision' problem, and to allow for 'controlled ball
                      // collisions' in humanoid class)

      bool collisionAnim = false;
      if (action.type == e_FunctionType_Movement || action.type == e_FunctionType_Trip || action.type == e_FunctionType_Sliding || action.type == e_FunctionType_Interfere || action.type == e_FunctionType_Deflect) collisionAnim = true;
      bool onlyWhenDirectionChangedUnexpectedly = false;
      if (action.type == e_FunctionType_Interfere || action.type == e_FunctionType_Deflect) onlyWhenDirectionChangedUnexpectedly = true;

      bool directionChangedUnexpectedly = false;
      if (onlyWhenDirectionChangedUnexpectedly) {
        DO_VALIDATION;
        float unexpectedDistance = (GetMentalImage(players[i]->GetController()->GetReactionTime_ms() + action.elapsedTime_ms)->GetBallPrediction(1000) - GetBall()->Predict(1000)).GetLength(); // mental image from when the action began
        if (unexpectedDistance > 0.5f) directionChangedUnexpectedly = true;
      }


      if (collisionAnim && !players[i]->HasUniquePossession() &&
          (onlyWhenDirectionChangedUnexpectedly ==
           directionChangedUnexpectedly)) {
        DO_VALIDATION;

        float boundingBoxSizeOffset = -0.1f; // fake a big AABB for more blocking fun, or a small one for less bouncy bounce
        if (!players[i]->HasPossession()) boundingBoxSizeOffset += 0.03f; else
                                          boundingBoxSizeOffset -= 0.03f;

        if (action.type == e_FunctionType_Sliding ||
            action.type == e_FunctionType_Interfere) {
          DO_VALIDATION;
          boundingBoxSizeOffset += 0.1f;
        }
        if (action.type == e_FunctionType_Deflect) {
          DO_VALIDATION;
          boundingBoxSizeOffset += 0.2f;
        }

        if (((players[i]->GetPosition() + Vector3(0, 0, 0.8f)) -
             ball->Predict(0))
                .GetLength() < 2.5f) {
          DO_VALIDATION;  // premature optimization is the root of all evil :D
          const PlayerBodyCollider body =
              BuildBodyCollider(players[i]->GetKinematicState());
          for (const BodyVolume *volume : body.GetVolumes()) {
            DO_VALIDATION;
            float ballRadius = ball->Entity().Profile().radius + boundingBoxSizeOffset;
            if (volume->IntersectsSphere(ball->Predict(0), ballRadius)) {
              DO_VALIDATION;
              if (players[i] == players[i]
                                    ->GetTeam()
                                    ->GetDesignatedTeamPossessionPlayer() &&
                  GetLastTouchBias(200) < 0.01f) {
                DO_VALIDATION;
                players[i]->TriggerControlledBallCollision();
              } else {
                float movementBias = oppLastTouchBias * 0.8f + 0.2f;
                bounceVec +=
                    (ball->Predict(0) - volume->center)
                        .GetNormalized(Vector3(0)) *
                        movementBias +
                    players[i]->GetMovement() * (1.0f - movementBias);
                bounceCount++;
                players[i]->GetTeam()->SetLastTouchPlayer(
                    players[i], e_TouchType_Accidental);
                // Centrality of the hit, relative to the volume radius. The
                // legacy code normalised by the AABB half diagonal, which
                // inflates the scale for elongated parts; the primitive radius
                // is the more meaningful body extent.
                bias += (1.0f -
                         clamp(((ball->Predict(0) - volume->center).GetLength() -
                                ballRadius) /
                                   volume->radius,
                               0.0f, 1.0f)) *
                            0.9f +
                        0.1f;
              }
            }
          }
        }
      }
    }
  }

  if (bias > 0.0f) {
    DO_VALIDATION;
    bounceVec /= (bounceCount * 1.0f);
    bounceVec.coords[2] *= 0.6f;
    bounceVec.Normalize();
    Vector3 currentMovement = ball->GetMovement();
    Vector3 fullCollisionVec = (bounceVec * 6.0f) + (bounceVec * currentMovement.GetLength() * 0.6f) + (currentMovement * -0.2f);
    bias = clamp(bias, 0.0f, 1.0f);
    bias = bias * 0.5f + 0.5f;
    Vector3 resultVector = fullCollisionVec * bias + currentMovement * (1.0f - bias);
    if (resultVector.GetLength() > currentMovement.GetLength()) resultVector = resultVector.GetNormalized(0) * currentMovement.GetLength();
    //resultVector = resultVector.GetNormalized(0) * (currentMovement.GetLength() * 0.7f + resultVector.GetLength() * 0.3f); // EXPERIMENT!
    resultVector *= 0.7f;

    ball->Touch(resultVector);
    ball->SetRotation(boostrandom(-30, 30), boostrandom(-30, 30),
                      boostrandom(-30, 30), 0.5f * bias);
    lastBodyBallCollisionTime_ms = actualTime_ms;
  }
}



void Match::BumpActualTime_ms(unsigned long time) {
  if (IsInPlay()) {
    DO_VALIDATION;
    matchTime_ms += time * (1.0f / matchDurationFactor);
  }
  actualTime_ms += time;
  if (IsGoalScored()) goalScoredTimer += time; else goalScoredTimer = 0;

  if (IsInPlay() && !IsInSetPiece()) {
    DO_VALIDATION;
    GetMatchData()->AddPossessionTime(teams[0] == designatedPossessionPlayer->GetTeam() ? 0 : 1, time);
  }
}
