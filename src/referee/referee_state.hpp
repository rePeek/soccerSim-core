#ifndef FOOTBALL_SIM_REFEREE_REFEREE_STATE_HPP
#define FOOTBALL_SIM_REFEREE_REFEREE_STATE_HPP

#include "football/foundation/math/vector3.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/player/player_definition.hpp"

using namespace football::math;

// Referee-owned rule state: the values the rules must carry across ticks to
// relate facts over time. Physics and actors never read or write them; only the
// referee mutates them and Simulation applies the resulting rulings. Restart
// execution state lives in Runtime::RestartContext, never here.
//
// A foul keeps stable PlayerId identities plus the values the verdict needs. It
// never retains a Player object: live rule data is supplied per call through
// FoulLiveFacts.
struct Foul {
  football::player::PlayerId offender = football::player::kInvalidPlayerId;
  football::player::PlayerId victim = football::player::kInvalidPlayerId;
  int offender_team_id = -1;
  int victim_team_id = -1;
  int foulType = 0;  // 0: nothing, 1: foul, 2: yellow, 3: red
  bool advantage = false;
  football::sim::Tick foul_tick{};
  Vector3 foulPosition;
  bool hasBeenProcessed = false;
};

// One team's live rule facts for a foul verdict. Fading possession is
// deliberately sampled at the evaluation instant: an advantage is only kept
// while the fouled team's possession recovers after the contact.
struct FoulTeamFacts {
  int team_id = -1;
  int static_side = 0;
  float fading_possession_amount = 1.0f;
};

// Read-only rule view for one CheckFoul call, borrowed from the caller's team
// facts. Indexed by legacy team slot (0/1).
struct FoulLiveFacts {
  FoulTeamFacts home;
  FoulTeamFacts away;

  const FoulTeamFacts& For(int team_id) const { return team_id == 0 ? home : away; }
};

#endif  // FOOTBALL_SIM_REFEREE_REFEREE_STATE_HPP
