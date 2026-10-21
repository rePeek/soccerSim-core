// Player-owned authorization port. Referee owns the rule logic; Runtime adapts
// it behind this interface and Player only consumes the result. The request is
// value-only and never names a Referee/TouchState/RestartContext type.
#ifndef FOOTBALL_PLAYER_AUTHORIZATION_HPP
#define FOOTBALL_PLAYER_AUTHORIZATION_HPP

#include <span>

#include "football/foundation/math/vector3.hpp"

class Player;

namespace football::sim::player {

struct PlayerActionAuthorization {
  // True when a scheduled restart is in flight for this actor's match state.
  bool restart_gate = false;
  bool movement_allowed = true;
  bool action_allowed = true;
  bool goalkeeper_hands_allowed = false;
};

// A single control translation request. Runtime samples its live rule facts at
// Evaluate() time, so the decision is made where the original checks ran.
struct PlayerAuthorizationRequest {
  const Player* actor = nullptr;
  std::span<Player* const> teammates;
  int team_id = -1;
  bool save_request = false;
  bool is_goalkeeper = false;
  bool ball_retained = false;
  football::math::Vector3 predicted_ball;
  float half_length = 0.0f;
  int execution_direction = -1;
};

class PlayerAuthorizationPort {
 public:
  virtual ~PlayerAuthorizationPort() = default;
  virtual PlayerActionAuthorization Evaluate(
      const PlayerAuthorizationRequest& request) const = 0;
};

}  // namespace football::sim::player

#endif  // FOOTBALL_PLAYER_AUTHORIZATION_HPP
