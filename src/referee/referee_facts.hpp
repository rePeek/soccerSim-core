#ifndef FOOTBALL_SIM_RULES_REFEREE_FACTS_HPP
#define FOOTBALL_SIM_RULES_REFEREE_FACTS_HPP

#include "football/foundation/math/vector3.hpp"
#include "football/player/player_definition.hpp"
#include "football/match/game_mode.hpp"
#include "football/match/team_side.hpp"
#include "referee/restart_phase.hpp"

namespace football::sim::rules {

// One team's read-only rule facts for a single evaluation instant. Runtime
// samples them from the live TeamView; the Referee never reaches into Team,
// Player or Runtime state.
struct RefereeTeamFact {
  int team_id = -1;
  football::sim::TeamSide side = football::sim::TeamSide::Home;
  int defending_direction = 0;
  // Live possession weight, sampled this instant (never frozen at a contact).
  float fading_possession_amount = 1.0f;
};

// The football's read-only rule facts for one evaluation instant.
struct RefereeBallFact {
  football::math::Vector3 position;
  football::math::Vector3 velocity;
  // Physical sphere radius. The rules need it to decide whether the whole
  // ball crossed a line; it is sampled from the Ball model, never guessed.
  float radius = 0.11f;
};

// Read-only restart execution facts exposed to the rules. Runtime owns the full
// restart context; the rules only need the kind, side, phase and taker identity.
struct RestartRuleFacts {
  bool active = false;
  bool scheduled = false;  // a restart state exists (phase is meaningful)
  football::sim::RestartPhase phase = football::sim::RestartPhase::InPlay;
  e_GameMode kind = e_GameMode_Normal;
  int team_id = -1;
  football::player::PlayerId taker = football::player::kInvalidPlayerId;
};

// Everything the rules need to decide whether an accepted touch legally returns
// the ball to play. Runtime samples it at the exact touch boundary; moving the
// sample earlier or later can change the release verdict.
struct RestartReleaseFacts {
  bool eligible = false;  // restart Ready and this toucher is the taker
  e_GameMode kind = e_GameMode_Normal;
  int action_type = 0;
  bool scheduled_contact = false;
  bool physical_kick = false;
  bool intentional_kick = false;  // taker's last accepted touch was a kick
  float ball_speed = 0.0f;
};

}  // namespace football::sim::rules

#endif  // FOOTBALL_SIM_RULES_REFEREE_FACTS_HPP
