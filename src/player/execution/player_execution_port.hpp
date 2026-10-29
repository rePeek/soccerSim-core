// PEX-3: the narrow port PlayerExecution uses to read the player entity and to
// submit authoritative state changes back to it.
//
// Design rules this header encodes:
//   - Execution never stores the port. It is bound for the duration of one
//     call (see PlayerExecution::PortScope) and released before returning, so
//     a "Player* reachable from anywhere in the executor" cannot re-form.
//   - Reads are capability, identity, match state and the committed kinematic
//     state. Writes are the few commits whose evaluation belongs to execution
//     but whose authority belongs to the Player: the kinematic state, the
//     legacy root-motion adoption and a factual position correction.
//   - `Actor()` exists only for the two consumers that are keyed by the entity
//     itself: the pass solver (it queries players by pointer) and the
//     Runtime-owned rule authorization port. It is not a general back door:
//     gameplay facts are read through the calls above.
#ifndef FOOTBALL_SIM_PLAYER_EXECUTION_PORT_HPP
#define FOOTBALL_SIM_PLAYER_EXECUTION_PORT_HPP

#include <optional>

#include "football/contact/player_ground_collider.hpp"
#include "football/contact/player_touch_type.hpp"
#include "football/foundation/math/vector3.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/player/player_body_profile.hpp"
#include "football/player/player_definition.hpp"
#include "football/player/player_kinematics.hpp"
#include "football/match/team_side.hpp"
#include "football/player/execution/player_motion_profile.hpp"
#include "player/execution/player_command.hpp"
#include "player/execution/player_team_view.hpp"

class Player;

// Read/write surface of the owning Player, valid only inside one execution
// call. Player is the only implementation.
class PlayerExecutionPort {
 public:
  virtual ~PlayerExecutionPort() = default;

  // --- identity and definition -----------------------------------------
  virtual football::player::PlayerId GetID() const = 0;
  virtual int GetTeamID() const = 0;
  virtual football::sim::TeamSide GetTeamSide() const = 0;
  virtual const football::player::PlayerBodyProfile& BodyProfile() const = 0;
  virtual bool IsActive() const = 0;

  // --- capability ------------------------------------------------------
  virtual float GetStat(football::player::PlayerStat name) const = 0;
  virtual float GetMaxVelocity() const = 0;
  virtual int GetReactionTime_ms() const = 0;

  // --- authoritative motion (read) --------------------------------------
  virtual const PlayerKinematicState& GetKinematicState() const = 0;
  virtual Vector3 GetPosition() const = 0;
  virtual Vector3 GetDirectionVec() const = 0;

  // --- player match state (read) ----------------------------------------
  virtual football::sim::Tick GetLastTouchTick() const = 0;
  virtual float GetLastTouchBias(int decay_ms, football::sim::Tick at) const = 0;
  virtual bool IsControlledBallCollisionTriggered() const = 0;
  virtual e_TouchType GetLastTouchType() const = 0;
  // The actor's own desired interception time (intent), not a world relation.
  virtual int DesiredTimeToBall_ms() const = 0;

  // --- player match state (write, execution-owned consequences) ---------
  virtual void TriggerControlledBallCollision() = 0;
  virtual void ResetControlledBallCollisionTrigger() = 0;
  // A reset clears the touch record: the next tick must not inherit the touch
  // history of the previous situation.
  virtual void ResetTouchRecord() = 0;

  // --- authoritative commits --------------------------------------------
  // Player remains the single writer of PlayerKinematicState. Execution
  // computes; these entries commit.
  virtual PlayerKinematicState CommitLocomotion(const PlayerCommand& command,
                                               const PlayerKinematicState& tick_start,
                                               bool restart_needs_simulation,
                                               float dt) = 0;
  virtual void CommitLegacyActionMotion() = 0;
  // Fatal in all builds: the committed kinematics and the compatibility
  // projection must agree at every sync point. Evaluated by the Player, because
  // only it owns both sides.
  virtual void CheckKinematicOracle() const = 0;
  virtual void ApplyPositionCorrection(const football::math::Vector3& offset) = 0;

  // --- entity identity, for pointer-keyed consumers only -----------------
  virtual Player& Actor() = 0;
  virtual const Player& Actor() const = 0;
};

#endif  // FOOTBALL_SIM_PLAYER_EXECUTION_PORT_HPP
