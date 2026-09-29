#ifndef FOOTBALL_CORE_CONTACT_BALL_CONTROL_CONSTRAINT_HPP
#define FOOTBALL_CORE_CONTACT_BALL_CONTROL_CONSTRAINT_HPP

#include "core/model/player/player.hpp"

namespace football::contact {

// Foundation for short-lived ball-control authority. Identity is a Player
// profile attribute, never a team slot or a transient state field.
enum class BallControlType { Trap, Catch, Carry };

struct BallControlConstraint {
  football::model::PlayerId owner = football::model::kInvalidPlayerId;
  BallControlType type = BallControlType::Trap;

  bool IsActive() const { return owner != football::model::kInvalidPlayerId; }
};

}  // namespace football::contact

#endif  // FOOTBALL_CORE_CONTACT_BALL_CONTROL_CONSTRAINT_HPP
