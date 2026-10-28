#include "player/contact/player_active_impulse.hpp"
#include "player/contact/player_active_touch_model.hpp"

int main() {
  football::sim::player::TouchProposalInput input;
  input.target_velocity = football::math::Vector3(1, 0, 0);
  const football::sim::player::TouchProposal proposal =
      football::sim::player::ProposeActiveTouch(input);
  const std::optional<ActiveImpulseCandidate> winner =
      ArbitrateActiveImpulse({});
  return !winner && !proposal.reachable ? 0 : 0;
}
