// A confirmed touch produced by an actor, plus the write-only sink that
// publishes it. Player owns these values; the Event layer receives them and
// Runtime records them. No Event implementation type appears in Player.
#ifndef FOOTBALL_SIM_PLAYER_TOUCH_EVENT_HPP
#define FOOTBALL_SIM_PLAYER_TOUCH_EVENT_HPP

#include "football/foundation/math/vector3.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/match/team_side.hpp"
#include "football/player/state/player_definition.hpp"
#include "football/contact/player_touch_type.hpp"

namespace football::sim::player {

struct PlayerTouchEvent {
  Tick touched_at{};
  football::player::PlayerId player = football::player::kInvalidPlayerId;
  TeamSide team = TeamSide::Home;
  e_TouchType type = e_TouchType_None;
  football::math::Vector3 ball_position;
  football::math::Vector3 ball_velocity;
  int action_type = 0;
  // Authorized selected kick action plus genuine approaching boot CCD impact.
  bool physical_kick = false;
};

class PlayerTouchSink {
 public:
  virtual ~PlayerTouchSink() = default;
  virtual void OnAcceptedTouch(const PlayerTouchEvent& touch) = 0;
};

}  // namespace football::sim::player

#endif  // FOOTBALL_SIM_PLAYER_TOUCH_EVENT_HPP
