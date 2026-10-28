// Runtime adapter for the Player-owned authorization port. It is the only place
// that translates live Referee facts into the Player value, so the rule logic
// stays in the rules domain and Player never names it.
#ifndef FOOTBALL_SIM_RUNTIME_PLAYER_AUTHORIZATION_PORT_HPP
#define FOOTBALL_SIM_RUNTIME_PLAYER_AUTHORIZATION_PORT_HPP

#include "event/touch_state.hpp"
#include "player/execution/player_authorization.hpp"
#include "runtime/restart_context.hpp"

namespace football::sim {

class PlayerAuthorizationPortImpl final
    : public football::sim::player::PlayerAuthorizationPort {
 public:
  PlayerAuthorizationPortImpl(const event::TouchState& touches,
                              const RestartContext& restart)
      : touches_(touches), restart_(restart) {}

  football::sim::player::PlayerActionAuthorization Evaluate(
      const football::sim::player::PlayerAuthorizationRequest& request) const override;

 private:
  const event::TouchState& touches_;
  const RestartContext& restart_;
};

}  // namespace football::sim

#endif  // FOOTBALL_SIM_RUNTIME_PLAYER_AUTHORIZATION_PORT_HPP
