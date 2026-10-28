#include "football/contact/player_active_impulse.hpp"
#include "football/contact/player_active_touch_model.hpp"
#include "football/contact/player_contact_kernel.hpp"
#include "football/contact/player_action_volume.hpp"
#include "football/contact/passive_body_touch.hpp"
#include "football/contact/contact_evidence.hpp"
// The flat compatibility path still resolves to the canonical Contact value.
#include "football/player/foul_assessment.hpp"

int main() {
  football::sim::player::TouchProposalInput input;
  input.target_velocity = football::math::Vector3(1, 0, 0);
  const auto proposal = football::sim::player::ProposeActiveTouch(input);
  if (!proposal.reachable || ArbitrateActiveImpulse({})) return 1;
  const auto response = football::contact::FinishPassiveBodyTouches({}, {});
  if (response.velocity) return 2;
  const auto fall = football::contact::AttributeStandingFall({1, 0, 0}, {2, 1, 0});
  return fall ? 3 : 0;
}
