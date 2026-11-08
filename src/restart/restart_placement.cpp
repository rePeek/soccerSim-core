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

#include "restart/restart_placement.hpp"

#include <cassert>
#include <vector>

#include "football/ball/ball.hpp"
#include "player/execution/roster_query.hpp"
#include "player/player.hpp"

using football::ball::Ball;

namespace {

const FormationEntry& EntryFor(const football::sim::TeamView& team,
                               Player* player) {
  const FormationEntry* entry = football::sim::FormationEntryFor(team, player);
  assert(entry != nullptr && "kickoff placement requires a formation entry");
  return *entry;
}

}  // namespace

Player* PositionKickOffPlayers(const football::pitch::Pitch& pitch,
                              const football::sim::TeamView& team,
                              const football::sim::TeamView& other_team,
                              int kickoff_taker_team_id, int taker_team_id,
                              const Ball& ball,
                              football::sim::TickSpan regulation,
                              const MatchOptions& options) {
  // Only the opening/half-time ceremony uses direct placement. Ordinary
  // restarts are planned by PlanRestart and executed by RestartController.
  // Preserve placement order, including intermediate ResetPosition calls:
  // those calls also reset the player's action/animation state.
  std::vector<Player*> players;
  for (Player* actor : team.players) {
    if (actor->IsActive()) players.push_back(actor);
  }

  auto iter = players.begin();
  while (iter != players.end()) {
    Vector3 focus = ball.state().position.Get2D();
    if (EntryFor(team, *iter).role == e_PlayerRole_GK) {
      focus.coords[0] = 0;
      focus.coords[1] = 0;
      (*iter)->ResetPosition(
          Vector3(pitch.half_length() * team.defending_direction * 0.98, 0, 0), focus);
      iter = players.erase(iter);
    } else {
      iter++;
    }
  }

  const bool is_taker_team = taker_team_id == team.team_id;
  if (is_taker_team) team.SetFadingPossessionAmount(1.5);
  else team.SetFadingPossessionAmount(0.5);

  for (unsigned int i = 0; i < players.size(); i++) {
    Vector3 base_pos =
        EntryFor(team, players[i]).position *
        Vector3(-team.defending_direction * pitch.half_length() * 0.6,
                -team.defending_direction * pitch.half_width() * 0.6, 0);
    // Deterministic stagger, alternating by roster slot.
    base_pos.coords[1] +=
        (i % 2 == 0 ? -1.0f : 1.0f) *
        (1.0f + 0.25f * static_cast<float>(i));
    base_pos.coords[0] *= 0.5;
    base_pos.coords[0] +=
        (pitch.half_length() * 0.2) * team.defending_direction;
    if (base_pos.coords[0] * team.defending_direction < 0.5)
      base_pos.coords[0] = 0.5 * team.defending_direction;
    if (base_pos.GetLength() < 9.4) {
      base_pos.Normalize(Vector3(team.defending_direction, 0, 0));
      base_pos *= 9.4;
    }
    players[i]->ResetPosition(base_pos, ball.state().position.Get2D());

    if (is_taker_team) {
      std::vector<Player*> result;
      football::sim::player::FindClosestPlayers(team.players, Vector3(0), result, 2);
      for (unsigned int i = 0; i < result.size(); i++) {
        result[i]->ResetPosition(
            Vector3(0, i * 1.4 * team.defending_direction, 0),
            ball.state().position.Get2D());
      }
    }
  }

  auto formation_players = team.players;
  auto players_to_position = team.players;
  if (regulation > football::sim::TickSpan{} &&
      other_team.players.size() == team.players.size() &&
      (options.left_team_owns_ball ^ kickoff_taker_team_id == 0)) {
    formation_players = other_team.players;
  }
  assert(formation_players.size() == players_to_position.size());
  const bool use_other_formation =
      formation_players.data() == other_team.players.data();
  const football::sim::TeamView& formation_team =
      use_other_formation ? other_team : team;
  for (int x = 0; x < formation_players.size(); x++) {
    if (players_to_position[x]->IsActive()) {
      Vector3 base_pos =
          EntryFor(formation_team, formation_players[x]).start_position *
          Vector3(-team.defending_direction * pitch.half_length(),
                  -team.defending_direction * pitch.half_width(), 0);
      players_to_position[x]->ResetPosition(base_pos, ball.state().position.Get2D());
    }
  }

  if (!is_taker_team) return nullptr;
  std::vector<Player*> candidates;
  football::sim::player::FindClosestPlayers(
      team.players, ball.state().position.Get2D(), candidates, 2);
  return candidates[0];
}
