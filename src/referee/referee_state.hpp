#ifndef FOOTBALL_SIM_REFEREE_REFEREE_STATE_HPP
#define FOOTBALL_SIM_REFEREE_REFEREE_STATE_HPP

#include "football/foundation/math/vector3.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/player/state/player_definition.hpp"
#include "referee/foul_sanction.hpp"

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
  // The severity the card came from. A send-off is only downgraded for the
  // penalty double jeopardy when the severity itself is not excessive.
  football::sim::rules::FoulSeverity severity =
      football::sim::rules::FoulSeverity::None;
  bool advantage = false;
  // Advantage played out: the restart is cancelled but discipline still runs.
  bool advantage_kept = false;
  // Law 12 extras the sanction classification needs at the decision point.
  bool dogso = false;
  bool attempted_to_play_ball = false;
  football::sim::Tick foul_tick{};
  Vector3 foulPosition;
  bool hasBeenProcessed = false;
  // A realized advantage cancels the restart but not the discipline: the card
  // is shown at the next stoppage, which Runtime signals by settling a stop.
  int pending_card = 0;
  football::player::PlayerId pending_offender = football::player::kInvalidPlayerId;
};

// One team's live rule facts for a foul verdict. Fading possession is
// deliberately sampled at the evaluation instant: an advantage is only kept
// while the fouled team's possession recovers after the contact.
struct FoulTeamFacts {
  int team_id = -1;
  int defending_direction = 0;
  float fading_possession_amount = 1.0f;
};

// Read-only rule view for one CheckFoul call, borrowed from the caller's team
// facts. Indexed by legacy team slot (0/1).
struct FoulLiveFacts {
  FoulTeamFacts home;
  FoulTeamFacts away;

  const FoulTeamFacts& For(int team_id) const { return team_id == 0 ? home : away; }
};

// A player frozen on an offside position at the last attacking touch: stable
// identity plus the team needed to detect an opponent deflection. No Player
// object.
struct OffsideReference {
  football::player::PlayerId player = football::player::kInvalidPlayerId;
  int team_id = -1;
};

// The offside position captured at the instant an attacking team last played
// the ball (Law 11). A defender's deliberate play resets it; a deflection or a
// deliberate save leaves it standing, so a later flagged attacker is still
// penalised.
struct OffsideSnapshot {
  football::sim::Tick created_at{};
  football::player::PlayerId passer = football::player::kInvalidPlayerId;
  int attacking_team_id = -1;
  std::vector<OffsideReference> candidates;

  bool Empty() const { return candidates.empty() && attacking_team_id < 0; }
  void Clear() {
    created_at = {};
    passer = football::player::kInvalidPlayerId;
    attacking_team_id = -1;
    candidates.clear();
  }
};

#endif  // FOOTBALL_SIM_REFEREE_REFEREE_STATE_HPP
