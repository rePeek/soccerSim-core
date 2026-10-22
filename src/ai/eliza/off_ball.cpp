// Ported from the legacy off-the-ball role strategies
// (default_def/default_mid/default_off) and AI_GetMindSet. The role dispatch,
// static/dynamic anchor blend, support blend, defensive component and offside
// trap became pure functions over WorldState + TacticalBoard.
#include "off_ball.hpp"

#include <algorithm>
#include <cmath>

#include "defense.hpp"
#include "eliza_observation.hpp"
#include "football/foundation/math/scalar.hpp"
#include "support_position.hpp"

namespace football::ai::eliza {
namespace {
using football::math::clamp;
using football::math::Vector3;
}  // namespace

float MindSet(e_PlayerRole role) {
  switch (role) {
    case e_PlayerRole_GK:
    case e_PlayerRole_CB:
      return 0.0f;
    case e_PlayerRole_LB:
    case e_PlayerRole_RB:
    case e_PlayerRole_DM:
      return 0.25f;
    case e_PlayerRole_LM:
    case e_PlayerRole_CM:
    case e_PlayerRole_RM:
      return 0.5f;
    case e_PlayerRole_AM:
      return 0.75f;
    case e_PlayerRole_CF:
      return 1.0f;
  }
  return 0.5f;
}

Vector3 CalculateFormationAnchor(const WorldState& world,
                                 const EffectivePlayerTactics* tactics,
                                 const WorldPlayerState& self) {
  const float defend = static_cast<float>(TeamOf(world, self.side).defending_direction);
  if (tactics != nullptr && tactics->goalkeeper)
    return Vector3(defend * (world.pitch.half_length() - 2.0f),
                   clamp(world.ball_position.coords[1] * 0.2f, -3.0f, 3.0f), 0.0f);
  if (tactics == nullptr) return self.position;
  // The interpreter already produced this slot's anchor in pitch metres with
  // the plan's width, line and role profile applied.
  Vector3 position = tactics->target_anchor_metres;
  const float attack = -defend;
  const bool team_has_ball = TeamOf(world, self.side).has_possession;
  position.coords[0] += world.ball_position.coords[0] * 0.2f +
                        attack * (team_has_ball ? 8.0f : -3.0f);
  position.coords[1] += world.ball_position.coords[1] * 0.15f;
  position.coords[0] = clamp(position.coords[0], -world.pitch.half_length() + 2.0f,
                             world.pitch.half_length() - 2.0f);
  position.coords[1] = clamp(position.coords[1], -world.pitch.half_width() + 2.0f,
                             world.pitch.half_width() - 2.0f);
  return position;
}

Vector3 CalculateOffBallTarget(const WorldState& world,
                               const EffectivePlayerTactics* tactics,
                               const WorldPlayerState& self,
                               const ElizaParameters& parameters,
                               const ElizaIntent& intent) {
  const float mindset = MindSet(self.role);
  const Vector3 anchor = CalculateFormationAnchor(world, tactics, self);
  const float possession = TeamOf(world, self.side).fading_possession_amount;
  const float attack_direction =
      -static_cast<float>(TeamOf(world, self.side).defending_direction);

  float attack_bias =
      clamp((possession - 0.5f) * (0.6f + 0.8f * mindset), 0.05f, 0.75f);
  attack_bias = clamp(attack_bias * (1.0f + 0.35f * intent.transition_on_win),
                      0.02f, 0.85f);
  Vector3 target = anchor;
  if (attack_bias > 0.05f) {
    const bool make_run = mindset >= 0.75f && possession >= 1.05f &&
        self.position.coords[0] * attack_direction > world.pitch.half_length() * 0.05f;
    const Vector3 support = CalculateSupportPosition(world, anchor, self.id, mindset,
                                                     make_run, parameters);
    target = anchor * (1.0f - attack_bias) + support * attack_bias;
  }

  float defence_bias = std::pow(clamp(1.5f - mindset - possession, 0.0f, 1.0f), 0.7f);
  // pressing_intensity scales how hard the team engages; the transition
  // instruction adds the short counter-press / regroup tilt.
  defence_bias = ElizaDefenceBias(defence_bias, intent);
  if (defence_bias > 0.0f && !IsKeeper(self)) {
    const WorldPlayerState* opponent = SelectMarkingTarget(world, self, tactics);
    if (opponent != nullptr)
      target = ApplyDefensiveComponent(world, self, *opponent, target, defence_bias);
  }

  const float offside_x = OffsideLineX(world, self.side);
  target.coords[0] = ApplyOffsideConstraint(target.coords[0], offside_x, attack_direction,
                                            0.05f, world.pitch.half_length());
  target.coords[1] = clamp(target.coords[1], -world.pitch.half_width(),
                           world.pitch.half_width());
  return target;
}

}  // namespace football::ai::eliza
