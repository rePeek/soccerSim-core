#ifndef FOOTBALL_CORE_PHYSICS_CONTACT_BALL_BALL_IMPACT_HPP
#define FOOTBALL_CORE_PHYSICS_CONTACT_BALL_BALL_IMPACT_HPP

#include <cstdint>

#include "core/model/player/player.hpp"
#include "core/physics/contact/ball/ball_contact.hpp"

namespace football_sim::contact {

// Where a resolved contact came from. Rules and possession consume this
// attribution without contact physics knowing anything about teams.
enum class BallContactSourceType { Ground, Woodwork, PlayerBody };

struct BallContactSource {
  BallContactSourceType type = BallContactSourceType::Ground;
  football_sim::PlayerId player = football_sim::kInvalidPlayerId;
};

struct BallImpact {
  BallContact contact;
  BallContactSource source;
};

}  // namespace football_sim::contact

#endif  // FOOTBALL_CORE_PHYSICS_CONTACT_BALL_BALL_IMPACT_HPP
