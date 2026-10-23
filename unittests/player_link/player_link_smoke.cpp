// Independent Player boundary smoke test.
//
// Two properties are enforced here:
//  1. The public Player value headers compile with ONLY the top-level include
//     tree (no src/ root) and the declared lower-layer contracts.
//  2. The whole football_player archive links with only its declared lower
//     dependencies (foundation/geometry/pitch/ball/animation/collision/BqLog).
//     Any Player -> Runtime/Referee/Event/Observation undefined symbol makes
//     this executable fail to link, so it also proves the link closure.
#include <array>
#include <chrono>

#include "football/foundation/time/tick.hpp"
#include "football/match/team_side.hpp"
#include "football/player/formation_entry.hpp"
#include "football/player/foul_assessment.hpp"
#include "football/player/player_control.hpp"
#include "football/player/player_control_set.hpp"
#include "football/player/player_definition.hpp"
#include "football/player/player_restart_facts.hpp"
#include "football/player/player_role.hpp"
#include "football/player/player_team_match_facts.hpp"
#include "football/player/player_touch_facts.hpp"
#include "football/player/player_touch_type.hpp"

int main() {
  football::player::PlayerDefinition definition;
  definition.id = 7;

  PlayerControl control;
  control.player = definition.id;
  control.action = ControlAction::None;

  PlayerControlSet control_set;
  control_set.Set(definition.id, control);

  football::player::PlayerTouchFacts touch_facts;
  touch_facts.last_team = 1;
  touch_facts.last_player_by_team[1] = definition.id;

  football::player::PlayerTeamMatchFacts team_facts;
  team_facts.has_possession = true;
  team_facts.time_to_ball_ms = 42;

  football::player::PlayerRestartFacts restart_facts;
  restart_facts.active = false;

  FormationEntry entry;
  entry.role = e_PlayerRole_CF;

  const football::sim::ChallengeAction challenge =
      football::sim::ChallengeAction::StandingChallenge;

  const e_TouchType touch_type = e_TouchType_Intentional_Kicked;
  const football::sim::TeamSide side = football::sim::TeamSide::Home;

  return (touch_type == e_TouchType_SIZE || side == football::sim::TeamSide::Away ||
          team_facts.time_to_ball_ms == 0 || restart_facts.active ||
          challenge == football::sim::ChallengeAction::Other)
             ? 1
             : 0;
}
