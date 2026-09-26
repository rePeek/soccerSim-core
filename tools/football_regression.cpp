#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>

#include "game_env.hpp"
#include "onthepitch/player/legacy_locomotion_command.hpp"
#include "onthepitch/player/player_kinematics.hpp"
#include "onthepitch/player/player_locomotion.hpp"
#include "onthepitch/player/player_ground_collider.hpp"
#include "onthepitch/player/player_action_executor.hpp"
#include "onthepitch/player/player_action_volume.hpp"
#include "onthepitch/player/player_body_collider.hpp"
#include "onthepitch/player/player_retain_anchor.hpp"
#include "onthepitch/player/humanoid/animcollection.hpp"
#include "onthepitch/player/humanoid/import_hierarchy.hpp"
#include "onthepitch/player/humanoid/import_loader.hpp"

namespace {

constexpr float kFloatTolerance = 1e-5f;

struct RegressionFailure : std::exception {
  explicit RegressionFailure(const std::string& message) : message(message) {}
  const char* what() const noexcept override { return message.c_str(); }
  std::string message;
};

void Require(bool condition, const std::string& message) {
  if (!condition) throw RegressionFailure(message);
}

void RequireNear(float actual, float expected, const std::string& label) {
  if (std::fabs(actual - expected) > kFloatTolerance) {
    std::ostringstream message;
    message << label << ": expected " << expected << ", got " << actual;
    throw RegressionFailure(message.str());
  }
}

void RequirePositionNear(const Position& actual, const Position& expected,
                         const std::string& label) {
  for (int axis = 0; axis < 3; ++axis) {
    RequireNear(actual.env_coord(axis), expected.env_coord(axis),
                label + "[" + std::to_string(axis) + "]");
  }
}

void RequireInfoEqual(const SharedInfo& actual, const SharedInfo& expected,
                      const std::string& label) {
  Require(actual.step == expected.step, label + ": step differs");
  Require(actual.left_goals == expected.left_goals,
          label + ": left score differs");
  Require(actual.right_goals == expected.right_goals,
          label + ": right score differs");
  Require(actual.game_mode == expected.game_mode, label + ": game mode differs");
  Require(actual.is_in_play == expected.is_in_play,
          label + ": in-play state differs");
  Require(actual.ball_owned_team == expected.ball_owned_team,
          label + ": ball owner team differs");
  Require(actual.ball_owned_player == expected.ball_owned_player,
          label + ": ball owner player differs");
  RequirePositionNear(actual.ball_position, expected.ball_position,
                      label + ": ball position");
  RequirePositionNear(actual.ball_direction, expected.ball_direction,
                      label + ": ball direction");
  RequirePositionNear(actual.ball_rotation, expected.ball_rotation,
                      label + ": ball rotation");

  const auto compare_players = [&](const std::vector<PlayerInfo>& current,
                                   const std::vector<PlayerInfo>& reference,
                                   const char* team_name) {
    Require(current.size() == reference.size(),
            label + ": " + team_name + " player count differs");
    for (size_t index = 0; index < current.size(); ++index) {
      const PlayerInfo& player = current[index];
      const PlayerInfo& expected_player = reference[index];
      const std::string player_label = label + ": " + team_name +
          " player " + std::to_string(index);
      RequirePositionNear(player.player_position, expected_player.player_position,
                          player_label + " position");
      RequirePositionNear(player.player_direction, expected_player.player_direction,
                          player_label + " direction");
      RequireNear(player.tired_factor, expected_player.tired_factor,
                  player_label + " tired factor");
      Require(player.has_card == expected_player.has_card,
              player_label + " card differs");
      Require(player.is_active == expected_player.is_active,
              player_label + " active state differs");
      Require(player.designated_player == expected_player.designated_player,
              player_label + " designated state differs");
      Require(player.role == expected_player.role, player_label + " role differs");
    }
  };

  compare_players(actual.left_team, expected.left_team, "left");
  compare_players(actual.right_team, expected.right_team, "right");
  Require(actual.left_controllers == expected.left_controllers,
          label + ": left controllers differ");
  Require(actual.right_controllers == expected.right_controllers,
          label + ": right controllers differ");
}

uint64_t HashBytes(uint64_t hash, const void* bytes, size_t size) {
  const auto* value = static_cast<const unsigned char*>(bytes);
  for (size_t index = 0; index < size; ++index) {
    hash ^= value[index];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

template <typename T>
uint64_t HashValue(uint64_t hash, const T& value) {
  return HashBytes(hash, &value, sizeof(value));
}

uint64_t HashPosition(uint64_t hash, const Position& position) {
  for (int axis = 0; axis < 3; ++axis) {
    const float value = position.env_coord(axis);
    hash = HashValue(hash, value);
  }
  return hash;
}

uint64_t HashInfo(const SharedInfo& info) {
  uint64_t hash = UINT64_C(1469598103934665603);
  hash = HashPosition(hash, info.ball_position);
  hash = HashPosition(hash, info.ball_direction);
  hash = HashPosition(hash, info.ball_rotation);
  hash = HashValue(hash, info.left_goals);
  hash = HashValue(hash, info.right_goals);
  hash = HashValue(hash, info.game_mode);
  hash = HashValue(hash, info.is_in_play);
  hash = HashValue(hash, info.ball_owned_team);
  hash = HashValue(hash, info.ball_owned_player);
  hash = HashValue(hash, info.step);

  const auto hash_players = [&](const std::vector<PlayerInfo>& players) {
    uint64_t result = HashValue(hash, players.size());
    for (const PlayerInfo& player : players) {
      result = HashPosition(result, player.player_position);
      result = HashPosition(result, player.player_direction);
      result = HashValue(result, player.has_card);
      result = HashValue(result, player.is_active);
      result = HashValue(result, player.designated_player);
      result = HashValue(result, player.tired_factor);
      result = HashValue(result, player.role);
    }
    return result;
  };
  hash = hash_players(info.left_team);
  hash = hash_players(info.right_team);

  const auto hash_controllers = [&](const std::vector<ControllerInfo>& controllers) {
    uint64_t result = HashValue(hash, controllers.size());
    for (const ControllerInfo& controller : controllers) {
      result = HashValue(result, controller.controlled_player);
    }
    return result;
  };
  hash = hash_controllers(info.left_controllers);
  hash = hash_controllers(info.right_controllers);
  return hash;
}

void CheckPlayerKinematics() {
  PlayerKinematicState state;
  PlayerKinematicParameters parameters;
  parameters.maxSpeed = 10.0f;
  parameters.acceleration = 10.0f;
  parameters.braking = 5.0f;
  parameters.maxTurnRate = 1.0f;

  PlayerKinematicInput input;
  input.desiredVelocity = Vector3(10.0f, 0.0f, 0.0f);
  input.desiredFacing = Vector3(1.0f, 0.0f, 0.0f);
  PlayerKinematics::Step(state, input, parameters, 0.1f);

  RequireNear(state.velocity.coords[0], 1.0f,
              "kinematics acceleration velocity");
  RequireNear(state.position.coords[0], 0.1f,
              "kinematics acceleration position");
  RequireNear(state.speed, 1.0f, "kinematics acceleration speed");
  Require(state.facing.GetDotProduct(input.desiredFacing) > 0.0f,
          "kinematics should turn toward the desired facing");

  input.desiredVelocity = Vector3(0);
  PlayerKinematics::Step(state, input, parameters, 0.1f);
  RequireNear(state.velocity.coords[0], 0.5f,
              "kinematics braking velocity");
  RequireNear(state.position.coords[0], 0.15f,
              "kinematics braking position");
  RequireNear(state.velocity.coords[2], 0.0f,
              "kinematics planar velocity");
  RequireNear(state.position.coords[2], 0.0f,
              "kinematics planar position");
}

// H3e1b: the procedural locomotion model. These tests pin the structures the
// legacy measurements justified: strongly asymmetric speed approach, turning
// paid for with speed, rate-limited heading change, velocity-derived facing,
// idle facing fallback and planarity. They also pin that the model stays
// continuous, because the legacy per-tick texture was an animation artifact
// that the new simulation must not reproduce.
void CheckProceduralLocomotion() {
  PlayerLocomotionParameters parameters;
  parameters.maxSpeed = 7.5f;
  parameters.acceleration = 6.0f;
  parameters.braking = 12.0f;
  parameters.maxTurnRate = 6.0f;
  parameters.turnRateSpeedFactor = 0.75f;
  parameters.turnSpeedLoss = 0.5f;
  parameters.idleSpeedThreshold = 0.5f;

  const auto require_planar = [](const PlayerKinematicState& state,
                                 const char* label) {
    RequireNear(state.position.coords[2], 0.0f,
                std::string(label) + " planar position");
    RequireNear(state.velocity.coords[2], 0.0f,
                std::string(label) + " planar velocity");
    RequireNear(state.facing.coords[2], 0.0f,
                std::string(label) + " planar facing");
  };

  // Continuous acceleration: one tick reaches acceleration * dt, nowhere near
  // the desired speed. An animation switch would have snapped instead.
  {
    PlayerKinematicState state;
    state.facing = Vector3(1.0f, 0.0f, 0.0f);
    PlayerLocomotionInput input;
    input.desiredVelocity = Vector3(7.5f, 0.0f, 0.0f);
    input.idleFacing = Vector3(1.0f, 0.0f, 0.0f);
    PlayerLocomotion::Step(state, input, parameters, 0.01f);
    RequireNear(state.speed, 0.06f,
                "procedural locomotion: first tick speed");
    RequireNear(state.position.GetLength(), 0.0006f,
                "procedural locomotion: first tick distance");
    require_planar(state, "procedural locomotion acceleration");
  }

  // Braking uses the braking rate, not the acceleration rate: the asymmetry
  // measured in legacy is a deliberate part of the model.
  {
    PlayerKinematicState state;
    state.velocity = Vector3(5.0f, 0.0f, 0.0f);
    state.facing = Vector3(1.0f, 0.0f, 0.0f);
    state.speed = 5.0f;
    PlayerLocomotionInput input;
    input.desiredVelocity = Vector3(0.0f);
    input.idleFacing = Vector3(1.0f, 0.0f, 0.0f);
    PlayerLocomotion::Step(state, input, parameters, 0.1f);
    RequireNear(state.speed, 3.8f,
                "procedural locomotion: braking uses the braking rate");
    require_planar(state, "procedural locomotion braking");
  }

  // The target is reached exactly, never overshot.
  {
    PlayerKinematicState state;
    state.facing = Vector3(1.0f, 0.0f, 0.0f);
    PlayerLocomotionInput input;
    input.desiredVelocity = Vector3(1.0f, 0.0f, 0.0f);
    input.idleFacing = Vector3(1.0f, 0.0f, 0.0f);
    PlayerLocomotion::Step(state, input, parameters, 1.0f);
    RequireNear(state.speed, 1.0f,
                "procedural locomotion: target is not overshot");
    require_planar(state, "procedural locomotion target");
  }

  // Turning costs speed: a reversal has half the target speed, while a
  // straight command from the same state keeps full speed.
  {
    PlayerKinematicState turning;
    turning.velocity = Vector3(7.5f, 0.0f, 0.0f);
    turning.facing = Vector3(1.0f, 0.0f, 0.0f);
    turning.speed = 7.5f;
    PlayerLocomotionInput reversal;
    reversal.desiredVelocity = Vector3(-7.5f, 0.0f, 0.0f);
    reversal.idleFacing = Vector3(1.0f, 0.0f, 0.0f);
    PlayerLocomotion::Step(turning, reversal, parameters, 0.01f);
    RequireNear(turning.speed, 7.38f,
                "procedural locomotion: turning costs speed");

    PlayerKinematicState straight;
    straight.velocity = Vector3(7.5f, 0.0f, 0.0f);
    straight.facing = Vector3(1.0f, 0.0f, 0.0f);
    straight.speed = 7.5f;
    PlayerLocomotionInput ahead;
    ahead.desiredVelocity = Vector3(7.5f, 0.0f, 0.0f);
    ahead.idleFacing = Vector3(1.0f, 0.0f, 0.0f);
    PlayerLocomotion::Step(straight, ahead, parameters, 0.01f);
    RequireNear(straight.speed, 7.5f,
                "procedural locomotion: straight running keeps speed");
    require_planar(turning, "procedural locomotion turn penalty");
  }

  // The heading is rate limited. At full speed the model turns far more
  // slowly than the requested 90 degrees, which is the structure that
  // replaces legacy's animation-driven instant turns.
  {
    PlayerKinematicState state;
    state.velocity = Vector3(7.5f, 0.0f, 0.0f);
    state.facing = Vector3(1.0f, 0.0f, 0.0f);
    state.speed = 7.5f;
    PlayerLocomotionInput input;
    input.desiredVelocity = Vector3(0.0f, 7.5f, 0.0f);
    input.idleFacing = Vector3(1.0f, 0.0f, 0.0f);
    PlayerLocomotion::Step(state, input, parameters, 0.01f);
    const radian turned =
        std::fabs(Vector3(1.0f, 0.0f, 0.0f).GetAngle2D(
            state.velocity.GetNormalized(Vector3(1.0f, 0.0f, 0.0f))));
    RequireNear(turned, 0.015f,
                "procedural locomotion: heading is rate limited");
    require_planar(state, "procedural locomotion turn rate");
  }

  // Locomotion facing is the velocity direction while moving, which is the
  // legacy structural invariant, even if the state started with a stale
  // facing.
  {
    PlayerKinematicState state;
    state.velocity = Vector3(3.0f, 0.0f, 0.0f);
    state.facing = Vector3(0.0f, -1.0f, 0.0f);
    state.speed = 3.0f;
    PlayerLocomotionInput input;
    input.desiredVelocity = Vector3(3.0f, 0.0f, 0.0f);
    input.idleFacing = Vector3(0.0f, -1.0f, 0.0f);
    PlayerLocomotion::Step(state, input, parameters, 0.01f);
    Require(state.facing.GetDistance(
                state.velocity.GetNormalized(Vector3(1.0f, 0.0f, 0.0f))) <
                1e-6f,
            "procedural locomotion: facing is the velocity direction");
    require_planar(state, "procedural locomotion facing");
  }

  // Standing still holds the idle facing and does not drift.
  {
    PlayerKinematicState state;
    state.position = Vector3(2.0f, 3.0f, 0.0f);
    state.velocity = Vector3(0.0f);
    state.facing = Vector3(1.0f, 0.0f, 0.0f);
    state.speed = 0.0f;
    PlayerLocomotionInput input;
    input.desiredVelocity = Vector3(0.0f);
    input.idleFacing = Vector3(0.0f, -1.0f, 0.0f);
    PlayerLocomotion::Step(state, input, parameters, 0.01f);
    RequireNear(state.speed, 0.0f, "procedural locomotion: idle speed");
    RequireNear(state.position.coords[0], 2.0f,
                "procedural locomotion: idle does not drift x");
    RequireNear(state.position.coords[1], 3.0f,
                "procedural locomotion: idle does not drift y");
    RequireNear(state.facing.coords[1], -1.0f,
                "procedural locomotion: idle facing fallback");
    require_planar(state, "procedural locomotion idle");
  }
}

void CheckPlayerKinematicMirror() {
  PlayerKinematicState state;
  state.position = Vector3(1.0f, 2.0f, 0.0f);
  state.velocity = Vector3(3.0f, 4.0f, 0.0f);
  state.facing = Vector3(0.6f, 0.8f, 0.0f);
  state.speed = 5.0f;

  state.Mirror();
  RequireNear(state.position.coords[0], -1.0f, "mirror position x");
  RequireNear(state.position.coords[1], -2.0f, "mirror position y");
  RequireNear(state.velocity.coords[0], -3.0f, "mirror velocity x");
  RequireNear(state.velocity.coords[1], -4.0f, "mirror velocity y");
  // Legacy spatial-state mirroring negates position and movement only, so a
  // mirrored kinematic state must keep facing untouched.
  RequireNear(state.facing.coords[0], 0.6f, "mirror facing x");
  RequireNear(state.facing.coords[1], 0.8f, "mirror facing y");
  RequireNear(state.speed, 5.0f, "mirror speed");
}

void CheckPlayerGroundCollider() {
  PlayerGroundCollider first;
  first.SetCenter(Vector3(0.0f, 0.0f, 2.0f));
  RequireNear(first.center.coords[2], 0.0f,
              "ground collider should stay on the pitch plane");

  PlayerGroundCollider second;
  second.SetCenter(Vector3(0.71f, 0.0f, 0.0f));
  Require(first.Intersects(second),
          "ground colliders should intersect within their radii");

  second.SetCenter(Vector3(0.72f, 0.0f, 0.0f));
  Require(!first.Intersects(second),
          "ground colliders should not intersect at the radius boundary");
}

void CheckPlayerActionExecutor() {
  PlayerActionDefinition definition;
  definition.type = e_FunctionType_Shot;
  definition.durationTime_ms = 300;
  definition.contactTime_ms = 120;
  definition.contactPosition = Vector3(1.0f, -2.0f, 0.5f);

  PlayerActionState state;
  PlayerActionExecutor::Begin(state, definition);
  Require(state.type == e_FunctionType_Shot, "action executor type");
  Require(state.frame == 0 && state.frameCount == 30,
          "action executor initial frames");
  Require(state.elapsedTime_ms == 0 && state.durationTime_ms == 300,
          "action executor initial timing");
  Require(state.contactFrame == 12 && state.contactTime_ms == 120,
          "action executor contact timing");
  RequireNear(state.contactPosition.coords[0], 1.0f,
              "action executor contact position x");
  Require(state.IsContactPending() && !state.IsContactDue() &&
              !state.IsComplete(),
          "action executor initial phase");

  const PlayerActionStepResult beforeContact =
      PlayerActionExecutor::Step(state, 100);
  Require(state.elapsedTime_ms == 100 && state.frame == 10 &&
              state.IsContactPending(),
          "action executor pre-contact phase");
  Require(!beforeContact.contactTriggered && !beforeContact.completed,
          "action executor should not emit an early event");

  const PlayerActionStepResult atContact =
      PlayerActionExecutor::Step(state, 20);
  Require(state.elapsedTime_ms == 120 && state.frame == 12 &&
              !state.IsContactPending() && state.IsContactDue(),
          "action executor contact phase");
  Require(atContact.contactTriggered && !atContact.completed,
          "action executor should emit one contact event");

  const PlayerActionStepResult atCompletion =
      PlayerActionExecutor::Step(state, 500);
  Require(state.elapsedTime_ms == 300 && state.frame == 30 &&
              state.IsComplete(),
          "action executor completion phase");
  Require(!atCompletion.contactTriggered && atCompletion.completed,
          "action executor should emit one completion event");
}

// H3e1a: the exact authority boundary for procedural locomotion. Only plain
// Movement ticks with no scheduled contact and no ball retention may leave
// the animation root-motion path; everything else must keep it because the
// animation root also feeds the touch vector and impulse algorithms.
void CheckPureLocomotionBoundary() {
  PlayerActionState movement;
  movement.type = e_FunctionType_Movement;
  Require(movement.IsPureLocomotion(false),
          "pure locomotion: plain movement should be eligible");
  Require(!movement.IsPureLocomotion(true),
          "pure locomotion: retaining the ball must stay animation-driven");

  PlayerActionState contact = movement;
  contact.contactTime_ms = 120;
  contact.contactFrame = 12;
  Require(!contact.IsPureLocomotion(false),
          "pure locomotion: a scheduled contact must stay animation-driven");

  const e_FunctionType animated[] = {
      e_FunctionType_None,       e_FunctionType_BallControl,
      e_FunctionType_Trap,       e_FunctionType_ShortPass,
      e_FunctionType_LongPass,   e_FunctionType_HighPass,
      e_FunctionType_Header,     e_FunctionType_Shot,
      e_FunctionType_Deflect,    e_FunctionType_Catch,
      e_FunctionType_Interfere,  e_FunctionType_Trip,
      e_FunctionType_Sliding,    e_FunctionType_Special};
  for (e_FunctionType type : animated) {
    PlayerActionState action;
    action.type = type;
    Require(!action.IsPureLocomotion(false),
            "pure locomotion: a non-locomotion action must stay "
            "animation-driven");
  }
}

// H3e1c-1: the legacy command adapter. Legacy commands are continuous, but the
// old controller resolved anything below the idle switch to an idle animation
// whose root motion was effectively zero. That deadband is command semantics,
// so it lives in the adapter and not in PlayerLocomotion. Above it the speed
// stays continuous and must not be snapped to the dribble/walk/sprint vertex
// values, because the class is an animation-selection regime and not a
// physical quantity.
void CheckLegacyLocomotionCommandAdapter() {
  PlayerKinematicState state;
  state.position = Vector3(1.0f, 2.0f, 0.0f);
  state.facing = Vector3(1.0f, 0.0f, 0.0f);
  const Vector3 body_facing(0.0f, -1.0f, 0.0f);
  const float max_speed = 8.0f;

  PlayerCommand command;
  command.useDesiredMovement = true;
  command.desiredDirection = Vector3(1.0f, 0.0f, 0.0f);

  // Below the idle switch the actor must stand still.
  command.desiredVelocityFloat = 0.5f;
  Require(FloatToEnumVelocity(0.5f) == e_Velocity_Idle,
          "legacy adapter test sanity: 0.5 m/s is the idle class");
  {
    const PlayerLocomotionInput input = BuildLegacyLocomotionInput(
        command, state, max_speed, body_facing);
    RequireNear(input.desiredVelocity.GetLength(), 0.0f,
                "legacy adapter: idle deadband");
    RequireNear(input.idleFacing.coords[1], -1.0f,
                "legacy adapter: body facing fallback");
  }

  // Above the deadband the speed is continuous: the class is a selection
  // regime, so 4.5 m/s must not become the walk vertex of 5.0.
  const float continuous_speeds[] = {1.8f, 2.6f, 3.4f, 4.5f, 5.7f, 6.9f, 8.0f};
  for (float speed : continuous_speeds) {
    Require(FloatToEnumVelocity(speed) != e_Velocity_Idle,
            "legacy adapter test sanity: speed above the deadband");
    command.desiredVelocityFloat = speed;
    const PlayerLocomotionInput input = BuildLegacyLocomotionInput(
        command, state, max_speed, body_facing);
    RequireNear(input.desiredVelocity.GetLength(), speed,
                "legacy adapter: continuous speed is not snapped");
  }

  // The actor's own maximum speed still caps the command.
  command.desiredVelocityFloat = 100.0f;
  {
    const PlayerLocomotionInput input = BuildLegacyLocomotionInput(
        command, state, max_speed, body_facing);
    RequireNear(input.desiredVelocity.GetLength(), max_speed,
                "legacy adapter: speed is clamped to the actor maximum");
  }

  // desiredLookAt only supplies the facing to hold while standing.
  command.desiredVelocityFloat = 0.1f;
  command.useDesiredLookAt = true;
  command.desiredLookAt = Vector3(1.0f, 4.0f, 0.0f);
  {
    const PlayerLocomotionInput input = BuildLegacyLocomotionInput(
        command, state, max_speed, body_facing);
    RequireNear(input.desiredVelocity.GetLength(), 0.0f,
                "legacy adapter: lookAt does not create locomotion");
    RequireNear(input.idleFacing.coords[0], 0.0f,
                "legacy adapter: lookAt facing x");
    RequireNear(input.idleFacing.coords[1], 1.0f,
                "legacy adapter: lookAt facing y");
  }
}

void CheckPlayerActionVolume() {
  PlayerActionState action;
  PlayerKinematicState kinematics;
  kinematics.position = Vector3(0.0f, 0.0f, 0.0f);
  kinematics.facing = Vector3(0.0f, -1.0f, 0.0f);

  PlayerActionVolumeParameters parameters;
  parameters.contactWindowStartFrame = 5;
  parameters.contactWindowEndFrame = 28;

  // Non-reaching actions never produce a volume.
  action.type = e_FunctionType_Movement;
  action.frame = 10;
  Require(!BuildTackleVolume(action, kinematics, parameters).active,
          "movement should not have a tackle volume");

  // Outside the contact window the volume is inactive too.
  action.type = e_FunctionType_Sliding;
  action.frame = 5;
  Require(!BuildTackleVolume(action, kinematics, parameters).active,
          "slide before the contact window should be inactive");
  action.frame = 28;
  Require(!BuildTackleVolume(action, kinematics, parameters).active,
          "slide after the contact window should be inactive");

  // Inside the window a slide reaches forward along facing.
  action.frame = 12;
  const PlayerActionVolume slide =
      BuildTackleVolume(action, kinematics, parameters);
  Require(slide.active, "slide inside the contact window should be active");
  RequireNear(slide.axis.coords[1], -1.0f, "slide axis should follow facing y");

  PlayerGroundCollider victim;
  const float reach = parameters.slideReach + parameters.slideRadius +
                      victim.radius;
  victim.SetCenter(Vector3(0.0f, -1.0f, 0.0f));
  Require(slide.Intersects(victim), "slide should reach a victim in front");

  // The capsule starts at the actor, so a victim behind is not reached.
  victim.SetCenter(Vector3(0.0f, 1.0f, 0.0f));
  Require(!slide.Intersects(victim), "slide should not reach behind");

  // Reach plus radii, inclusive boundary.
  victim.SetCenter(Vector3(0.0f, -(reach - 0.01f), 0.0f));
  Require(slide.Intersects(victim), "slide should reach just inside its boundary");
  victim.SetCenter(Vector3(0.0f, -(reach + 0.01f), 0.0f));
  Require(!slide.Intersects(victim), "slide should not reach past its boundary");

  // Standing tackle is shorter and broader.
  action.type = e_FunctionType_Interfere;
  const PlayerActionVolume interfere =
      BuildTackleVolume(action, kinematics, parameters);
  Require(interfere.active, "interfere should be active inside the window");
  Require(interfere.length < slide.length,
          "interfere should reach less far than a slide");
  Require(interfere.radius > slide.radius,
          "interfere should be broader than a slide");

  // Height is ignored: everything is evaluated on the pitch plane.
  victim.SetCenter(Vector3(0.0f, -1.0f, 2.0f));
  Require(slide.Intersects(victim), "tackle volume should be planar");
}

void CheckPlayerBodyCollider() {
  PlayerKinematicState kinematics;
  kinematics.position = Vector3(0.0f, 0.0f, 0.0f);
  const PlayerBodyCollider body = BuildBodyCollider(kinematics);

  Require(body.upperBody.center.coords[2] > body.lowerBody.center.coords[2],
          "torso should sit above the lower body");
  Require(body.head.center.coords[2] > body.upperBody.center.coords[2],
          "head should sit above the torso");

  // Body radii must stay body-sized: the ground contest radius is a contact
  // radius and is larger.
  const PlayerGroundCollider ground;
  Require(body.upperBody.radius < ground.radius,
          "body collider must not reuse the ground contest radius");

  const float ballRadius = 0.11f;

  // A ball beside the legs while the ball is at leg height is a hit.
  Require(body.lowerBody.IntersectsSphere(
              Vector3(body.lowerBody.radius + ballRadius - 0.01f, 0.0f,
                      body.lowerBody.center.coords[2]), ballRadius),
          "lower body should touch a ball beside the legs");

  // The torso sits high up, so it must not claim a ground ball.
  Require(!body.upperBody.IntersectsSphere(Vector3(1.0f, 0.0f, 0.11f), ballRadius),
          "torso should not touch a distant ground ball");
  Require(!body.head.IntersectsSphere(Vector3(0.0f, 0.0f, 0.11f), ballRadius),
          "head should not touch a ground ball");

  // Head-height ball is a head hit, not a leg hit.
  Require(body.head.IntersectsSphere(
              Vector3(0.0f, 0.0f, body.head.center.coords[2]), ballRadius),
          "head should touch a head height ball");
  Require(!body.lowerBody.IntersectsSphere(
              Vector3(0.0f, 0.0f, body.head.center.coords[2]), ballRadius),
          "lower body should not touch a head height ball");

  // The collider follows the player position and stays upright.
  PlayerKinematicState moved = kinematics;
  moved.position = Vector3(3.0f, -4.0f, 0.0f);
  moved.facing = Vector3(1.0f, 0.0f, 0.0f);
  const PlayerBodyCollider movedBody = BuildBodyCollider(moved);
  RequireNear(movedBody.lowerBody.center.coords[0], 3.0f,
              "body collider should follow position x");
  RequireNear(movedBody.lowerBody.center.coords[1], -4.0f,
              "body collider should follow position y");
  Require(!movedBody.upperBody.IntersectsSphere(Vector3(0.0f, 0.0f, 1.11f),
                                               ballRadius),
          "body collider should not stay at the origin");
}

void AppendDigestFloat(std::string& out, float value) {
  uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%08x", bits);
  out += buf;
}

template <typename T>
void AppendDigestInt(std::string& out, const T& value) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%lld|",
                static_cast<long long>(value));
  out += buf;
}

// A presentation-independent fingerprint of everything the next simulation
// tick may read. Deliberately includes the deterministic RNG *state*, not just
// a draw count: equal draw counts do not imply an equal stream.
std::string CaptureSimulationDigest(GameEnv& env) {
  std::string out;
  Match* match = env.context->gameTask->GetMatch();

  std::vector<Player*> players;
  match->GetActiveTeamPlayers(match->FirstTeam(), players);
  match->GetActiveTeamPlayers(match->SecondTeam(), players);
  std::vector<PlayerBase*> actors(players.begin(), players.end());
  std::vector<PlayerBase*> officials;
  match->GetOfficialPlayers(officials);
  actors.insert(actors.end(), officials.begin(), officials.end());

  AppendDigestInt(out, actors.size());
  for (const PlayerBase* actor : actors) {
    const PlayerKinematicState& kinematics = actor->GetKinematicState();
    AppendDigestFloat(out, kinematics.position.coords[0]);
    AppendDigestFloat(out, kinematics.position.coords[1]);
    AppendDigestFloat(out, kinematics.velocity.coords[0]);
    AppendDigestFloat(out, kinematics.velocity.coords[1]);
    AppendDigestFloat(out, kinematics.facing.coords[0]);
    AppendDigestFloat(out, kinematics.facing.coords[1]);
    const PlayerGroundCollider& ground = actor->GetGroundCollider();
    AppendDigestFloat(out, ground.center.coords[0]);
    AppendDigestFloat(out, ground.center.coords[1]);
    AppendDigestFloat(out, ground.radius);
    const PlayerActionState& action = actor->GetSimulationActionState();
    AppendDigestInt(out, static_cast<int>(action.type));
    AppendDigestInt(out, action.frame);
    AppendDigestInt(out, action.elapsedTime_ms);
    AppendDigestInt(out, action.contactTime_ms);
    AppendDigestFloat(out, action.contactPosition.coords[0]);
    AppendDigestFloat(out, action.contactPosition.coords[1]);
  }

  Ball* ball = match->GetBall();
  const Vector3 ballPos = ball->Predict(0);
  const Vector3 ballMomentum = ball->GetMovement();
  const Vector3 ballRotation = ball->GetRotation();
  AppendDigestFloat(out, ballPos.coords[0]);
  AppendDigestFloat(out, ballPos.coords[1]);
  AppendDigestFloat(out, ballPos.coords[2]);
  AppendDigestFloat(out, ballMomentum.coords[0]);
  AppendDigestFloat(out, ballMomentum.coords[1]);
  AppendDigestFloat(out, ballMomentum.coords[2]);
  AppendDigestFloat(out, ballRotation.coords[0]);
  AppendDigestFloat(out, ballRotation.coords[1]);
  AppendDigestFloat(out, ballRotation.coords[2]);

  AppendDigestInt(out, match->GetScore(0));
  AppendDigestInt(out, match->GetScore(1));
  AppendDigestInt(out, static_cast<int>(match->GetMatchPhase()));
  AppendDigestInt(out, match->IsInPlay() ? 1 : 0);
  AppendDigestInt(out, match->IsInSetPiece() ? 1 : 0);
  AppendDigestInt(out, match->GetLastTouchTeamID());
  AppendDigestInt(out, match->GetLastTouchPlayer() == nullptr ? -1 : 1);
  AppendDigestInt(out, match->GetDesignatedPossessionPlayer() == nullptr ? -1 : 1);
  AppendDigestInt(out, match->GetBallRetainer() == nullptr ? -1 : 1);

  const RefereeBuffer& buffer = match->GetReferee()->GetBuffer();
  AppendDigestInt(out, buffer.active ? 1 : 0);
  AppendDigestInt(out, buffer.stopTime);
  AppendDigestInt(out, buffer.prepareTime);
  AppendDigestInt(out, buffer.startTime);
  AppendDigestInt(out, static_cast<int>(buffer.desiredSetPiece));
  AppendDigestInt(out, match->GetReferee()->GetCurrentFoulType());
  AppendDigestFloat(out, buffer.restartPos.coords[0]);
  AppendDigestFloat(out, buffer.restartPos.coords[1]);

  // Simulation RNG state. This is part of simulation state, so it is also what
  // get_state/set_state saves; the draw count is diagnostics only and is
  // therefore reported separately rather than folded into the digest.
  std::ostringstream rngState;
  rngState << env.context->rng.engine();
  out += rngState.str();

  return out;
}

// The semantic digest is the reset contract. These named float fields are
// only diagnostics: they map a raw mismatch back to an authoritative actor
// field without weakening the byte-for-byte comparison. Do not compare the
// EnvState blob itself here: it serializes POD padding (including radian),
// which is transport representation rather than simulation state.
struct NamedDigestFloat {
  std::string name;
  uint32_t bits;
};

uint32_t FloatBits(float value) {
  uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

void AppendNamedDigestFloat(std::vector<NamedDigestFloat>& fields,
                            const std::string& name, float value) {
  fields.push_back({name, FloatBits(value)});
}

std::vector<NamedDigestFloat> CaptureResetDigestFloats(GameEnv& env) {
  std::vector<NamedDigestFloat> fields;
  Match* match = env.context->gameTask->GetMatch();

  const auto append_actor = [&](const PlayerBase* actor,
                                const std::string& actor_name) {
    const PlayerKinematicState& kinematics = actor->GetKinematicState();
    AppendNamedDigestFloat(fields, actor_name + ".kinematic.position.x",
                           kinematics.position.coords[0]);
    AppendNamedDigestFloat(fields, actor_name + ".kinematic.position.y",
                           kinematics.position.coords[1]);
    AppendNamedDigestFloat(fields, actor_name + ".kinematic.velocity.x",
                           kinematics.velocity.coords[0]);
    AppendNamedDigestFloat(fields, actor_name + ".kinematic.velocity.y",
                           kinematics.velocity.coords[1]);
    AppendNamedDigestFloat(fields, actor_name + ".kinematic.facing.x",
                           kinematics.facing.coords[0]);
    AppendNamedDigestFloat(fields, actor_name + ".kinematic.facing.y",
                           kinematics.facing.coords[1]);
    const PlayerGroundCollider& ground = actor->GetGroundCollider();
    AppendNamedDigestFloat(fields, actor_name + ".ground.center.x",
                           ground.center.coords[0]);
    AppendNamedDigestFloat(fields, actor_name + ".ground.center.y",
                           ground.center.coords[1]);
    AppendNamedDigestFloat(fields, actor_name + ".ground.radius",
                           ground.radius);
    const PlayerActionState& action = actor->GetSimulationActionState();
    AppendNamedDigestFloat(fields, actor_name + ".action.contactPosition.x",
                           action.contactPosition.coords[0]);
    AppendNamedDigestFloat(fields, actor_name + ".action.contactPosition.y",
                           action.contactPosition.coords[1]);
  };

  for (int team_id : {match->FirstTeam(), match->SecondTeam()}) {
    std::vector<Player*> players;
    match->GetActiveTeamPlayers(team_id, players);
    for (size_t index = 0; index < players.size(); ++index) {
      append_actor(players[index], "team[" + std::to_string(team_id) +
                                     "].player[" + std::to_string(index) + "]");
    }
  }
  std::vector<PlayerBase*> officials;
  match->GetOfficialPlayers(officials);
  for (size_t index = 0; index < officials.size(); ++index) {
    append_actor(officials[index], "official[" + std::to_string(index) + "]");
  }

  Ball* ball = match->GetBall();
  const Vector3 ball_position = ball->Predict(0);
  const Vector3 ball_momentum = ball->GetMovement();
  const Vector3 ball_rotation = ball->GetRotation();
  for (int axis = 0; axis < 3; ++axis) {
    const char axis_name[] = {'x', 'y', 'z'};
    AppendNamedDigestFloat(fields, std::string("ball.position.") + axis_name[axis],
                           ball_position.coords[axis]);
    AppendNamedDigestFloat(fields, std::string("ball.momentum.") + axis_name[axis],
                           ball_momentum.coords[axis]);
    AppendNamedDigestFloat(fields, std::string("ball.rotation.") + axis_name[axis],
                           ball_rotation.coords[axis]);
  }
  const RefereeBuffer& buffer = match->GetReferee()->GetBuffer();
  AppendNamedDigestFloat(fields, "referee.restartPosition.x",
                         buffer.restartPos.coords[0]);
  AppendNamedDigestFloat(fields, "referee.restartPosition.y",
                         buffer.restartPos.coords[1]);
  return fields;
}

std::string DescribeFirstResetDigestDifference(
    const std::vector<NamedDigestFloat>& first,
    const std::vector<NamedDigestFloat>& second) {
  if (first.size() != second.size()) {
    return "reset diagnostic field count differs";
  }
  for (size_t index = 0; index < first.size(); ++index) {
    if (first[index].bits == second[index].bits) continue;
    std::ostringstream message;
    message << first[index].name << ": reset #1 bits=0x" << std::hex
            << first[index].bits << ", reset #2 bits=0x" << second[index].bits;
    return message.str();
  }
  return "named float fields match; inspect non-float/raw digest state";
}



// Invariant #7: a presentation operation must not modify any state the next
// simulation tick reads.

ScenarioConfig MakeBuiltinAiConfig() {
  auto config = ScenarioConfig::make();
  config->left_agents = 0;
  config->right_agents = 0;
  config->real_time = false;
  config->deterministic = true;
  config->game_engine_random_seed = 42;
  return *config;
}

void Advance(GameEnv& env, int steps) {
  for (int step = 0; step < steps; ++step) env.step();
}

void WaitUntilInPlay(GameEnv& env, int max_steps, const std::string& label) {
  for (int step = 0; step < max_steps; ++step) {
    if (env.context->gameTask->GetMatch()->IsInPlay()) return;
    env.step();
  }
  Require(env.context->gameTask->GetMatch()->IsInPlay(),
          label + ": game did not resume");
}

void CheckKinematicMirrorConsistency(GameEnv& env, const std::string& label) {
  Match* match = env.context->gameTask->GetMatch();
  std::vector<Player*> players;
  match->GetActiveTeamPlayers(match->FirstTeam(), players);
  match->GetActiveTeamPlayers(match->SecondTeam(), players);
  std::vector<PlayerBase*> officials;
  match->GetOfficialPlayers(officials);
  Require(!players.empty(), label + ": missing active players");
  Require(!officials.empty(), label + ": missing officials");
  for (const Player* player : players) {
    Require(player->IsKinematicMirrorConsistent(),
            label + ": player kinematic mirror is stale");
  }
  for (const PlayerBase* official : officials) {
    Require(official->IsKinematicMirrorConsistent(),
            label + ": official kinematic mirror is stale");
  }
}

void CheckGoldenSnapshots(GameEnv& env, ScenarioConfig& config) {
  // Values below are a fixed-seed baseline. The hash covers every field in
  // SharedInfo; the explicit values make failures immediately diagnosable.
  struct GoldenSnapshot {
    int calls;
    int step;
    Position ball;
    Position left_player_0;
    Position right_player_0;
    int left_goals;
    int right_goals;
    bool is_in_play;
    uint64_t hash;
  };

  const GoldenSnapshot golden[] = {
      {1, -1, Position(0.0f, 0.0f, 0.110616393f, true),
       Position(-1.01102936f, 0.0f, 0.0f, true),
       Position(1.01102936f, 0.0f, 0.0f, true), 0, 0, false,
       UINT64_C(2178283517849602577)},
      {100, 79, Position(-0.409872681f, 0.273896456f, 0.108315744f, true),
       Position(-0.973170221f, 0.0185603499f, 0.0f, true),
       Position(0.83682096f, -0.000399013137f, 0.0f, true), 0, 0, true,
       UINT64_C(7453620862937221086)},
      {500, 458, Position(0.753526747f, 0.198903978f, 0.17945759f, true),
       Position(-0.831645966f, -6.32343508e-05f, 0.0f, true),
       Position(0.990424097f, 0.0361555517f, 0.0f, true), 0, 0, true,
       UINT64_C(12703816539953799179)},
      {1000, 910, Position(-0.038121134f, -0.13526684f, 0.157381654f, true),
       Position(-0.980546594f, -0.00378147163f, 0.0f, true),
       Position(0.828302026f, -0.000150032414f, 0.0f, true), 1, 0, true,
       UINT64_C(7247007324106109385)},
  };



  env.reset(config, false);
  int completed = 0;
  for (const GoldenSnapshot& expected : golden) {
    Advance(env, expected.calls - completed);
    completed = expected.calls;
    const SharedInfo info = env.get_info();
    const std::string label = "snapshot at call " + std::to_string(expected.calls);
    Require(info.step == expected.step, label + ": step differs");
    Require(info.left_goals == expected.left_goals &&
                info.right_goals == expected.right_goals,
            label + ": score differs");
    Require(info.is_in_play == expected.is_in_play, label + ": in-play differs");
    Require(!info.left_team.empty() && !info.right_team.empty(),
            label + ": missing active players");
    RequirePositionNear(info.ball_position, expected.ball, label + ": ball");
    RequirePositionNear(info.left_team.front().player_position,
                        expected.left_player_0, label + ": left player 0");
    RequirePositionNear(info.right_team.front().player_position,
                        expected.right_player_0, label + ": right player 0");
    CheckKinematicMirrorConsistency(env, label);
    const uint64_t hash = HashInfo(info);
    if (hash != expected.hash) {
      std::ostringstream message;
      message << label << ": expected hash " << expected.hash << ", got "
              << hash;
      throw RegressionFailure(message.str());
    }
  }
}

void CheckActionStateOracle(Match *match, const std::string &label) {
  for (int teamID = 0; teamID < 2; ++teamID) {
    std::vector<Player *> players;
    match->GetTeam(teamID)->GetActivePlayers(players);
    Require(!players.empty(), label + ": missing active players");
    for (const Player *player : players) {
      player->CheckSimulationActionOracle();
      const PlayerActionState &simulation =
          player->GetSimulationActionState();
      Require(player->GetCurrentFunctionType() == simulation.type,
              label + ": gameplay action type differs");
      Require(player->GetCurrentFrame() == simulation.frame,
              label + ": gameplay action frame differs");
      Require(player->GetTouchFrame() == simulation.contactFrame,
              label + ": gameplay contact frame differs");
      Require(player->TouchAnim() == simulation.HasScheduledContact(),
              label + ": gameplay contact schedule differs");
      Require(player->TouchPending() == simulation.IsContactPending(),
              label + ": gameplay pending-contact differs");
      for (int axis = 0; axis < 3; ++axis) {
        RequireNear(player->GetTouchPos().coords[axis],
                    simulation.contactPosition.coords[axis],
                    label + ": gameplay contact position");
      }
    }
  }
}

void CheckResetBitDeterminism(GameEnv& env, const ScenarioConfig& config) {
  const Vector3 caller_ball_position = config.ball_position;
  const auto require_caller_config_unchanged = [&]() {
    for (int axis = 0; axis < 3; ++axis) {
      Require(FloatBits(config.ball_position.coords[axis]) ==
                  FloatBits(caller_ball_position.coords[axis]),
              "reset at tick 0: reset mutated caller ball_position[" +
                  std::to_string(axis) + "]");
    }
  };

  env.reset(config, false);
  require_caller_config_unchanged();
  const std::string first_digest = CaptureSimulationDigest(env);
  const std::vector<NamedDigestFloat> first_fields =
      CaptureResetDigestFloats(env);

  env.reset(config, false);
  require_caller_config_unchanged();
  const std::string second_digest = CaptureSimulationDigest(env);
  const std::vector<NamedDigestFloat> second_fields =
      CaptureResetDigestFloats(env);

  Require(first_digest == second_digest,
          "reset at tick 0: simulation digest differs: " +
              DescribeFirstResetDigestDifference(first_fields, second_fields));
}

// H3e0c: the movement authority must be same-tick observable. A position
// correction (the primitive Match::CheckHumanoidCollisions uses) has to be
// visible immediately through every view of the actor, not committed at the
// end of the tick. This is the movement equivalent of the Ball::Touch
// same-tick visibility that H3d1 uncovered.
void CheckMovementAuthorityTiming(GameEnv& env, ScenarioConfig& config) {
  env.reset(config, false);
  WaitUntilInPlay(env, 60, "movement authority timing: kickoff");
  Match* match = env.context->gameTask->GetMatch();

  std::vector<Player*> players;
  match->GetActiveTeamPlayers(match->FirstTeam(), players);
  match->GetActiveTeamPlayers(match->SecondTeam(), players);
  Require(players.size() >= 2,
          "movement authority timing: missing active players");

  // A collision correction is immediately observable through the Player
  // kinematic state, the ground collider and the Humanoid spatial state.
  Player* corrected = players[0];
  const Vector3 before = corrected->GetPosition();
  const Vector3 offset(1.25f, -0.5f, 0.0f);
  corrected->OffsetPosition(offset);
  const Vector3 expected = before + offset;
  for (int axis = 0; axis < 3; ++axis) {
    Require(FloatBits(corrected->GetPosition().coords[axis]) ==
                FloatBits(expected.coords[axis]),
            "movement authority: OffsetPosition is not immediately visible "
            "through the Player kinematic state");
  }
  for (int axis = 0; axis < 2; ++axis) {
    Require(FloatBits(corrected->GetGroundCollider().center.coords[axis]) ==
                FloatBits(expected.coords[axis]),
            "movement authority: OffsetPosition left the collider behind");
  }
  Require(corrected->IsKinematicMirrorConsistent(),
          "movement authority: OffsetPosition left the kinematic mirror "
          "stale");

  // Corrections compose in call order: a later correction must observe the
  // position produced by the earlier one within the same tick.
  Player* chained = players[2];
  const Vector3 chain_start = chained->GetPosition();
  const Vector3 first_offset(0.5f, 0.25f, 0.0f);
  const Vector3 second_offset(-0.2f, 0.75f, 0.0f);
  chained->OffsetPosition(first_offset);
  chained->OffsetPosition(second_offset);
  const Vector3 chain_expected = chain_start + first_offset + second_offset;
  for (int axis = 0; axis < 3; ++axis) {
    Require(FloatBits(chained->GetPosition().coords[axis]) ==
                FloatBits(chain_expected.coords[axis]),
            "movement authority: chained corrections did not observe each "
            "other in call order");
  }
  Require(chained->IsKinematicMirrorConsistent(),
          "movement authority: chained corrections left the kinematic "
          "mirror stale");

  // Force an overlap, then run a real tick. Match::CheckHumanoidCollisions()
  // corrects positions after the actor ticks; the fatal movement oracle runs
  // inside that path, so a stale mirror there aborts the run.
  Player* p1 = players[0];
  Player* p2 = players[1];
  const Vector3 p1_position = p1->GetPosition();
  p2->OffsetPosition(p1_position + Vector3(0.15f, 0.0f, 0.0f) -
                     p2->GetPosition());
  Require(p1->GetGroundCollider().Intersects(p2->GetGroundCollider()),
          "movement authority: could not force the colliders to overlap");
  env.step();
  CheckKinematicMirrorConsistency(
      env, "kinematic mirror after an overlapped collision tick");
}

// H3e1a: measure how far the existing procedural locomotion model
// (PlayerKinematics) is from the legacy animation root motion over one tick of
// pure locomotion. This is deliberately a measurement, not a contract: it
// quantifies the semantic change H3e1b would accept, so the numbers are
// reported instead of asserted. Nothing in the simulation reads this.
void MeasureProceduralLocomotionDivergence(GameEnv& env,
                                           ScenarioConfig& config) {
  env.reset(config, false);
  WaitUntilInPlay(env, 60, "procedural locomotion divergence: kickoff");
  Match* match = env.context->gameTask->GetMatch();

  struct PendingSample {
    Player* player;
    PlayerKinematicState state;
    PlayerCommand command;
    int anim_id;
  };

  // Raw counts everything the boundary covers, including ticks that were not
  // a continuation of the sampled action (a requeue selects a new command
  // inside the tick) and ticks whose position was teleported or collision
  // corrected. Those are not model error, so the clean subset below is what
  // actually characterises the procedural model's per-tick gap.
  int samples = 0;
  int clean_samples = 0;
  int eligible_observations = 0;
  double clean_position_error_sum = 0.0;
  double clean_velocity_error_sum = 0.0;
  double clean_facing_error_sum = 0.0;
  double clean_max_position_error = 0.0;
  double clean_legacy_step_sum = 0.0;
  const double plausible_step_limit = 0.15;

  // H3e1b calibration. A procedural model cannot reproduce animation root
  // motion tick by tick, so it has to be designed against aggregate legacy
  // behaviour instead: the mapping from commanded speed to actual speed, the
  // achievable turn per tick as a function of facing error, and the speed
  // change achievable per tick as a function of the speed gap.
  const int desired_bucket_count = 9;  // 1 m/s bins
  int desired_count[9] = {0};
  double desired_sum_commanded[9] = {0.0};
  double desired_sum_legacy_speed[9] = {0.0};
  const int turn_bucket_count = 8;  // 0.25 rad bins
  int turn_count[8] = {0};
  double turn_sum_legacy[8] = {0.0};
  double turn_sum_procedural[8] = {0.0};
  const int gap_bucket_count = 9;  // 1 m/s bins over -4 .. +5
  int gap_count[9] = {0};
  double gap_sum_speed_change[9] = {0.0};
  double commanded_ratio_sum = 0.0;
  int commanded_ratio_samples = 0;
  double locomotion_position_error_sum = 0.0;
  double locomotion_velocity_error_sum = 0.0;
  double locomotion_facing_error_sum = 0.0;
  double locomotion_max_position_error = 0.0;
  int legacy_facing_velocity_samples = 0;
  double legacy_facing_velocity_error_sum = 0.0;
  int movement_facing_samples = 0;
  double movement_facing_error_sum = 0.0;

  const int ticks = 400;
  for (int tick = 0; tick < ticks; ++tick) {
    std::vector<Player*> players;
    match->GetActiveTeamPlayers(match->FirstTeam(), players);
    match->GetActiveTeamPlayers(match->SecondTeam(), players);

    std::vector<PendingSample> pending;
    for (Player* player : players) {
      if (player->IsEligibleForProceduralLocomotion()) {
        ++eligible_observations;
      }
      const Anim* anim = player->GetCurrentAnim();
      const PlayerCommand& command = anim->originatingCommand;
      if (!player->IsEligibleForProceduralLocomotion() ||
          !command.useDesiredMovement) {
        continue;
      }
      pending.push_back(PendingSample{player, player->GetKinematicState(),
                                      command, anim->id});
    }

    env.step();

    for (const PendingSample& sample : pending) {
      if (!sample.player->IsEligibleForProceduralLocomotion()) continue;
      PlayerKinematicState predicted = sample.state;
      PlayerKinematicInput input;
      const float desiredSpeed =
          clamp(sample.command.desiredVelocityFloat, 0.0f,
                sample.player->GetMaxVelocity());
      input.desiredVelocity =
          sample.command.desiredDirection.Get2D().GetNormalized(
              predicted.facing) *
          desiredSpeed;
      input.desiredFacing =
          input.desiredVelocity.GetNormalized(predicted.facing);
      if (sample.command.useDesiredLookAt) {
        input.desiredFacing =
            (sample.command.desiredLookAt - predicted.position)
                .Get2D()
                .GetNormalized(input.desiredFacing);
      }
      PlayerKinematicParameters parameters;
      parameters.maxSpeed = sample.player->GetMaxVelocity();
      PlayerKinematics::Step(predicted, input, parameters, 0.01f);
      const PlayerKinematicState& actual =
          sample.player->GetKinematicState();
      // The new procedural locomotion model, evaluated on exactly the same
      // sample so the two models are directly comparable.
      PlayerKinematicState predicted_locomotion = sample.state;
      const PlayerLocomotionInput locomotion_input =
          BuildLegacyLocomotionInput(sample.command, sample.state,
                                     sample.player->GetMaxVelocity(),
                                     sample.player->GetBodyDirectionVec());
      PlayerLocomotionParameters locomotion_parameters;
      locomotion_parameters.maxSpeed = sample.player->GetMaxVelocity();
      PlayerLocomotion::Step(predicted_locomotion, locomotion_input,
                             locomotion_parameters, 0.01f);
      const double locomotion_position_error =
          (predicted_locomotion.position - actual.position).GetLength();
      const double locomotion_velocity_error =
          (predicted_locomotion.velocity - actual.velocity).GetLength();
      const double locomotion_facing_error = std::fabs(
          predicted_locomotion.facing.GetAngle2D(actual.facing));
      const double position_error =
          (predicted.position - actual.position).GetLength();
      const double velocity_error =
          (predicted.velocity - actual.velocity).GetLength();
      const double facing_error =
          std::fabs(predicted.facing.GetAngle2D(actual.facing));
      ++samples;

      const double legacy_step =
          (actual.position - sample.state.position).GetLength();
      const bool action_continued =
          sample.player->GetCurrentAnim()->id == sample.anim_id;
      if (action_continued && legacy_step <= plausible_step_limit) {
        clean_position_error_sum += position_error;
        clean_velocity_error_sum += velocity_error;
        clean_facing_error_sum += facing_error;
        clean_legacy_step_sum += legacy_step;
        if (position_error > clean_max_position_error) {
          clean_max_position_error = position_error;
        }
        locomotion_position_error_sum += locomotion_position_error;
        locomotion_velocity_error_sum += locomotion_velocity_error;
        locomotion_facing_error_sum += locomotion_facing_error;
        if (locomotion_position_error > locomotion_max_position_error) {
          locomotion_max_position_error = locomotion_position_error;
        }
        ++clean_samples;

        const double current_speed = sample.state.velocity.GetLength();
        const double legacy_speed = actual.velocity.GetLength();
        const int desired_bucket = clamp(
            static_cast<int>(std::floor(desiredSpeed)), 0,
            desired_bucket_count - 1);
        ++desired_count[desired_bucket];
        desired_sum_commanded[desired_bucket] += desiredSpeed;
        desired_sum_legacy_speed[desired_bucket] += legacy_speed;
        if (desiredSpeed > 0.5f) {
          commanded_ratio_sum += legacy_speed / desiredSpeed;
          ++commanded_ratio_samples;
        }

        const double turn_error = std::fabs(
            sample.state.facing.GetAngle2D(input.desiredFacing));
        const int turn_bucket = clamp(
            static_cast<int>(turn_error / 0.25), 0, turn_bucket_count - 1);
        ++turn_count[turn_bucket];
        turn_sum_legacy[turn_bucket] += std::fabs(
            sample.state.facing.GetAngle2D(actual.facing));
        turn_sum_procedural[turn_bucket] += std::fabs(
            sample.state.facing.GetAngle2D(predicted.facing));

        const double speed_gap = desiredSpeed - current_speed;
        const int gap_bucket = clamp(
            static_cast<int>(std::floor(speed_gap + 4.0)), 0,
            gap_bucket_count - 1);
        ++gap_count[gap_bucket];
        gap_sum_speed_change[gap_bucket] += legacy_speed - current_speed;

        // Structural check: legacy facing is the movement direction while
        // moving, not a separately turned look direction. If that holds, a
        // procedural model should derive facing from velocity instead of
        // integrating a turn rate.
        // Mirror the exact legacy condition: directionVec equals the movement
        // direction only when the quantized velocity is not idle.
        const bool moving =
            sample.player->GetEnumVelocity() != e_Velocity_Idle;
        if (moving) {
          legacy_facing_velocity_error_sum += std::fabs(
              actual.facing.GetAngle2D(
                  actual.velocity.Get2D().GetNormalized(actual.facing)));
          ++legacy_facing_velocity_samples;
        }
        if (moving) {
          movement_facing_error_sum += std::fabs(
              actual.facing.GetAngle2D(
                  predicted.velocity.Get2D().GetNormalized(actual.facing)));
          ++movement_facing_samples;
        }
      }

      Require(predicted.position.coords[2] == 0.0f,
              "procedural locomotion: a planar prediction left the pitch "
              "plane");
      Require(predicted.velocity.coords[2] == 0.0f,
              "procedural locomotion: a planar prediction produced vertical "
              "velocity");
    }
  }

  Require(eligible_observations > 0,
          "procedural locomotion: the eligibility boundary never held, so "
          "the divergence measurement would be vacuous");
  Require(samples > 0,
          "procedural locomotion: no comparable locomotion tick was "
          "collected");
  Require(clean_samples > 0,
          "procedural locomotion: no continued-action locomotion tick was "
          "collected");
  std::cout << "locomotion divergence: eligible_observations="
            << eligible_observations << " samples=" << samples
            << " clean_samples=" << clean_samples
            << " clean_mean_position_error="
            << (clean_position_error_sum / clean_samples)
            << " clean_max_position_error=" << clean_max_position_error
            << " clean_mean_velocity_error="
            << (clean_velocity_error_sum / clean_samples)
            << " clean_mean_facing_error="
            << (clean_facing_error_sum / clean_samples)
            << " clean_mean_legacy_step="
            << (clean_legacy_step_sum / clean_samples) << "\n";
  std::cout << "locomotion divergence (new model): clean_samples="
            << clean_samples << " mean_position_error="
            << (locomotion_position_error_sum / clean_samples)
            << " max_position_error=" << locomotion_max_position_error
            << " mean_velocity_error="
            << (locomotion_velocity_error_sum / clean_samples)
            << " mean_facing_error="
            << (locomotion_facing_error_sum / clean_samples) << "\n";
  std::cout << "locomotion facing semantics: legacy_facing_vs_velocity_error="
            << (legacy_facing_velocity_samples > 0
                    ? legacy_facing_velocity_error_sum /
                          legacy_facing_velocity_samples
                    : 0.0)
            << " samples=" << legacy_facing_velocity_samples
            << " movement_facing_vs_legacy_error="
            << (movement_facing_samples > 0
                    ? movement_facing_error_sum / movement_facing_samples
                    : 0.0)
            << " samples=" << movement_facing_samples << "\n";
  std::cout << "locomotion calibration: commanded_ratio="
            << (commanded_ratio_samples > 0
                    ? commanded_ratio_sum / commanded_ratio_samples
                    : 0.0)
            << " commanded_ratio_samples=" << commanded_ratio_samples << "\n";
  for (int bucket = 0; bucket < desired_bucket_count; ++bucket) {
    if (desired_count[bucket] == 0) continue;
    std::cout << "  desired_speed_bucket[" << bucket << "] count="
              << desired_count[bucket] << " mean_commanded="
              << (desired_sum_commanded[bucket] / desired_count[bucket])
              << " mean_legacy_speed="
              << (desired_sum_legacy_speed[bucket] / desired_count[bucket])
              << "\n";
  }
  for (int bucket = 0; bucket < turn_bucket_count; ++bucket) {
    if (turn_count[bucket] == 0) continue;
    std::cout << "  facing_error_bucket[" << bucket << "] count="
              << turn_count[bucket] << " mean_legacy_turn="
              << (turn_sum_legacy[bucket] / turn_count[bucket])
              << " mean_procedural_turn="
              << (turn_sum_procedural[bucket] / turn_count[bucket]) << "\n";
  }
  for (int bucket = 0; bucket < gap_bucket_count; ++bucket) {
    if (gap_count[bucket] == 0) continue;
    std::cout << "  speed_gap_bucket[" << (bucket - 4) << "] count="
              << gap_count[bucket] << " mean_legacy_speed_change="
              << (gap_sum_speed_change[bucket] / gap_count[bucket]) << "\n";
  }
}

const char* VelocityClassName(e_Velocity velocity) {
  switch (velocity) {
    case e_Velocity_Idle:
      return "idle";
    case e_Velocity_Dribble:
      return "dribble";
    case e_Velocity_Walk:
      return "walk";
    case e_Velocity_Sprint:
      return "sprint";
  }
  return "unknown";
}

// H3e1b-prep2: two narrow questions, deliberately nothing else.
//
// 1. Velocity class is an animation-selection regime, not a physical value:
//    actual movement stays continuous. Does the current-class to
//    desired-class transition tell us something a single speed-gap bucket
//    cannot?
// 2. Is a large direction error handled by rotating the velocity, or by
//    braking it and building a new direction? Those are different models, so
//    they must be separated before any procedural model is designed.
void MeasureLocomotionRegimeTransitions(GameEnv& env,
                                        ScenarioConfig& config) {
  env.reset(config, false);
  WaitUntilInPlay(env, 60, "locomotion regimes: kickoff");
  Match* match = env.context->gameTask->GetMatch();

  struct TransitionCell {
    int samples = 0;
    double sum_current_speed = 0.0;
    double sum_next_speed = 0.0;
  };
  TransitionCell transitions[4][4];

  const int direction_bucket_count = 4;
  const double direction_bucket_limits[4] = {0.25, 0.75, 1.5, 3.2};
  int direction_count[4] = {0};
  double direction_sum_current_speed[4] = {0.0};
  double direction_sum_next_speed[4] = {0.0};
  double direction_sum_velocity_turn[4] = {0.0};
  double direction_sum_desired_speed[4] = {0.0};
  const double plausible_step_limit = 0.15;

  struct PendingSample {
    Player* player;
    PlayerKinematicState state;
    PlayerCommand command;
    int anim_id;
  };

  const int ticks = 400;
  for (int tick = 0; tick < ticks; ++tick) {
    std::vector<Player*> players;
    match->GetActiveTeamPlayers(match->FirstTeam(), players);
    match->GetActiveTeamPlayers(match->SecondTeam(), players);

    std::vector<PendingSample> pending;
    for (Player* player : players) {
      const Anim* anim = player->GetCurrentAnim();
      const PlayerCommand& command = anim->originatingCommand;
      if (!player->IsEligibleForProceduralLocomotion() ||
          !command.useDesiredMovement) {
        continue;
      }
      pending.push_back(PendingSample{player, player->GetKinematicState(),
                                      command, anim->id});
    }

    env.step();

    for (const PendingSample& sample : pending) {
      if (!sample.player->IsEligibleForProceduralLocomotion()) continue;
      const PlayerKinematicState& actual =
          sample.player->GetKinematicState();
      const double legacy_step =
          (actual.position - sample.state.position).GetLength();
      if (sample.player->GetCurrentAnim()->id != sample.anim_id ||
          legacy_step > plausible_step_limit) {
        continue;
      }

      const double current_speed = sample.state.velocity.GetLength();
      const double next_speed = actual.velocity.GetLength();
      const e_Velocity current_class =
          FloatToEnumVelocity(static_cast<float>(current_speed));
      const e_Velocity desired_class = FloatToEnumVelocity(clamp(
          sample.command.desiredVelocityFloat, 0.0f, sprintVelocity));
      TransitionCell& cell =
          transitions[static_cast<int>(current_class)]
                     [static_cast<int>(desired_class)];
      ++cell.samples;
      cell.sum_current_speed += current_speed;
      cell.sum_next_speed += next_speed;

      if (current_speed > 0.5) {
        const Vector3 current_direction =
            sample.state.velocity.Get2D().GetNormalized(sample.state.facing);
        const Vector3 desired_direction =
            sample.command.desiredDirection.Get2D().GetNormalized(
                current_direction);
        const double direction_error = std::fabs(
            current_direction.GetAngle2D(desired_direction));
        const double velocity_turn = std::fabs(current_direction.GetAngle2D(
            actual.velocity.Get2D().GetNormalized(current_direction)));
        int bucket = direction_bucket_count - 1;
        for (int i = 0; i < direction_bucket_count; ++i) {
          if (direction_error < direction_bucket_limits[i]) {
            bucket = i;
            break;
          }
        }
        ++direction_count[bucket];
        direction_sum_current_speed[bucket] += current_speed;
        direction_sum_next_speed[bucket] += next_speed;
        direction_sum_velocity_turn[bucket] += velocity_turn;
        direction_sum_desired_speed[bucket] += clamp(
            sample.command.desiredVelocityFloat, 0.0f, sprintVelocity);
      }
    }
  }

  std::cout << "locomotion regime transitions (current -> desired):\n";
  for (int from = 0; from < 4; ++from) {
    for (int to = 0; to < 4; ++to) {
      const TransitionCell& cell = transitions[from][to];
      if (cell.samples == 0) continue;
      std::cout << "  " << VelocityClassName(static_cast<e_Velocity>(from))
                << " -> " << VelocityClassName(static_cast<e_Velocity>(to))
                << " samples=" << cell.samples << " mean_current_speed="
                << (cell.sum_current_speed / cell.samples)
                << " mean_next_speed="
                << (cell.sum_next_speed / cell.samples) << "\n";
    }
  }

  std::cout << "locomotion direction error response:\n";
  for (int bucket = 0; bucket < direction_bucket_count; ++bucket) {
    if (direction_count[bucket] == 0) continue;
    std::cout << "  direction_error_lt_" << direction_bucket_limits[bucket]
              << " count=" << direction_count[bucket]
              << " mean_desired_speed="
              << (direction_sum_desired_speed[bucket] / direction_count[bucket])
              << " mean_current_speed="
              << (direction_sum_current_speed[bucket] / direction_count[bucket])
              << " mean_next_speed="
              << (direction_sum_next_speed[bucket] / direction_count[bucket])
              << " mean_velocity_turn="
              << (direction_sum_velocity_turn[bucket] /
                  direction_count[bucket])
              << "\n";
  }

  int total = 0;
  for (int from = 0; from < 4; ++from) {
    for (int to = 0; to < 4; ++to) total += transitions[from][to].samples;
  }
  Require(total > 0,
          "locomotion regimes: no clean sample was collected, the "
          "measurement would be vacuous");
}
void CheckResetAndStateRoundTrip(GameEnv& env, ScenarioConfig& config) {
  env.reset(config, false);
  CheckKinematicMirrorConsistency(env, "kinematic mirror after reset");
  Advance(env, 300);
  const SharedInfo first_reset = env.get_info();

  env.reset(config, false);
  Advance(env, 300);
  RequireInfoEqual(env.get_info(), first_reset, "repeat reset");
  CheckActionStateOracle(env.context->gameTask->GetMatch(),
                         "action state oracle after reset");
  CheckKinematicMirrorConsistency(env, "kinematic mirror after repeat reset");

  Advance(env, 75);
  const std::string serialized = env.get_state("");
  Advance(env, 125);
  const SharedInfo expected_after_restore = env.get_info();
  const std::string digest_after_restore = CaptureSimulationDigest(env);

  env.set_state(serialized);
  CheckActionStateOracle(env.context->gameTask->GetMatch(),
                         "action state oracle immediately after state restore");
  CheckKinematicMirrorConsistency(
      env, "kinematic mirror immediately after state restore");
  Advance(env, 125);
  RequireInfoEqual(env.get_info(), expected_after_restore, "state round-trip");
  Require(CaptureSimulationDigest(env) == digest_after_restore,
          "state round-trip: simulation digest differs (presentation RNG must "
          "not be part of simulation state)");
  CheckActionStateOracle(env.context->gameTask->GetMatch(),
                         "action state oracle after state restore");
  CheckKinematicMirrorConsistency(env, "kinematic mirror after state restore");
}

void CheckMatchTransitions(GameEnv& env, ScenarioConfig& config) {
  env.reset(config, false);
  Require(!env.get_info().is_in_play, "kickoff should begin paused");
  WaitUntilInPlay(env, 30, "initial kickoff");
  Advance(env, 5);  // Let kickoff's set-piece relaxation period elapse.

  Match* match = env.context->gameTask->GetMatch();
  match->GetBall()->SetPosition(
      Vector3(0.0f, pitchHalfH + lineHalfW + 0.5f, 0.5f));
  match->GetBall()->SetMomentum(Vector3(0));
  env.step();
  Require(!match->IsInPlay(), "throw-in should pause play");
  if (match->GetReferee()->GetBuffer().desiredSetPiece != e_GameMode_ThrowIn) {
    std::ostringstream message;
    message << "sideline exit scheduled set piece "
            << match->GetReferee()->GetBuffer().desiredSetPiece;
    throw RegressionFailure(message.str());
  }
  WaitUntilInPlay(env, 60, "throw-in restart");
  Advance(env, 5);  // Let throw-in's set-piece relaxation period elapse.

  match = env.context->gameTask->GetMatch();
  const int score_before = match->GetScore(0) + match->GetScore(1);
  match->GetBall()->SetPosition(
      Vector3(pitchHalfW + lineHalfW - 0.3f, 0.0f, 0.5f));
  match->GetBall()->SetMomentum(Vector3(50.0f, 0.0f, 0.0f));
  env.step();
  Require(match->GetScore(0) + match->GetScore(1) == score_before + 1,
          "crossing the goal line should score");
  Require(!match->IsInPlay(), "goal should pause play for kickoff");
  WaitUntilInPlay(env, 40, "goal kickoff restart");
}

}  // namespace

// The animation import used to be described by the scene graph. D4c2 replaced
// that with ImportNode/ImportHierarchy. The golden snapshots pin the
// end-to-end result of the import, but they do not pin the shape of the
// imported hierarchy or the behaviour of its lazy derived transforms, so
// those are checked here directly.
//
// The three properties below are the ones the simulation actually depends on:
//
//   1. the body-part anchors, and the order they are visited in. The touch
//      computation walks them and keeps the first one that is closest to the
//      ball, so this order decides ties.
//   2. the anchor names are disjoint from the body-part joint names: only
//      <node> entries become joints, only <geometry> entries become anchors.
//      The touch type of a ball touch is derived from the anchor name.
//   3. the derived-transform cache is a pure cache: recomputing it after
//      invalidation reproduces the same bits. If invalidation were missed the
//      cache would go stale and the import would silently change.
void CheckImportHierarchy() {
  ImportLoader loader;
  ImportHierarchy hierarchy =
      loader.LoadObject("media/objects/players/player.object");
  ImportNode* root = hierarchy.root.get();
  Require(root != nullptr, "import: the loader returned no root node");

  // Anchor order as written in the asset, which is the order the old
  // Node::GetObjects() produced too: anchors of a node first, then the child
  // nodes depth-first.
  const std::vector<std::string> expected_anchors = {
      "pelvis",        "trunk",          "head",           "left_upperarm",
      "left_lowerarm",  "right_upperarm", "right_lowerarm",  "left_upperleg",
      "left_lowerleg",  "left_foot",      "right_upperleg",  "right_lowerleg",
      "right_foot"};
  Require(hierarchy.anchors.size() == expected_anchors.size(),
          "import: unexpected number of body-part anchors");
  for (size_t i = 0; i < expected_anchors.size(); ++i) {
    Require(hierarchy.anchors[i]->kind == ImportNodeKind::Anchor,
            "import: a body-part anchor is not marked as one");
    if (hierarchy.anchors[i]->GetName() != expected_anchors[i]) {
      std::ostringstream message;
      message << "import: anchor " << i << " is '"
              << hierarchy.anchors[i]->GetName() << "', expected '"
              << expected_anchors[i] << "'";
      throw RegressionFailure(message.str());
    }
  }

  // The joints the animation poses. AnimCollection renames the root to
  // "player" before building this map, so do the same here.
  root->SetName("player");
  ImportNodeMap node_map;
  BuildImportNodeMap(root, node_map);
  const char* expected_joints[] = {
      "middle",       "neck",           "left_thigh",   "right_thigh",
      "left_knee",    "right_knee",     "left_ankle",   "right_ankle",
      "left_shoulder", "right_shoulder", "left_elbow",   "right_elbow",
      "body",         "player"};
  for (const char* joint : expected_joints) {
    Require(LookupImportNode(node_map, BodyPartFromString(joint)) != nullptr,
            std::string("import: joint not in the node map: ") + joint);
  }
  // An anchor must never also be a joint: the maps are built from disjoint
  // XML elements, and BodyPartFromString would be fatal on an anchor name.
  for (ImportNode* anchor : hierarchy.anchors) {
    for (const char* joint : expected_joints) {
      Require(anchor->GetName() != joint,
              std::string("import: anchor name collides with a joint: ") +
                  joint);
    }
  }

  // Derived transforms: a recomputation must reproduce the cached value
  // exactly, for every anchor. This is the property that makes the lazy cache
  // safe to use from the animation code.
  std::vector<Vector3> cached;
  for (const ImportNode* anchor : hierarchy.anchors) {
    cached.push_back(anchor->GetDerivedPosition());
  }
  root->UpdateDerivedTransforms();
  for (size_t i = 0; i < hierarchy.anchors.size(); ++i) {
    const Vector3 recomputed = hierarchy.anchors[i]->GetDerivedPosition();
    for (int axis = 0; axis < 3; ++axis) {
      Require(recomputed.coords[axis] == cached[i].coords[axis],
              "import: the derived transform cache is not a pure cache");
    }
  }

  // The hierarchy is really applied: moving the absolute root translates every
  // anchor by exactly that offset, and rotating a joint moves the anchors
  // below it.
  const Vector3 offset(3.0f, -2.0f, 0.5f);
  const Vector3 before = hierarchy.anchors.front()->GetDerivedPosition();
  root->SetPosition(offset);
  const Vector3 after = hierarchy.anchors.front()->GetDerivedPosition();
  for (int axis = 0; axis < 3; ++axis) {
    RequireNear(after.coords[axis], before.coords[axis] + offset.coords[axis],
                "import: translating the root did not translate an anchor");
  }

  //
  // Rotating a joint must move the anchors below it. Note that an anchor with a
  // zero local offset (which is how the asset writes most of them: a named
  // marker at the joint origin) sits exactly on its parent, so the rotation has
  // to come from further up the chain -- the head anchor moves with the middle
  // joint because the neck in between has a non-zero offset.
  ImportNode* middle_joint =
      LookupImportNode(node_map, BodyPartFromString("middle"));
  Require(middle_joint != nullptr, "import: no middle joint to rotate");
  const Vector3 head_before = hierarchy.anchors[2]->GetDerivedPosition();
  Quaternion rotated;
  rotated.SetAngleAxis(1.0f, Vector3(0, 0, 1));
  middle_joint->SetRotation(rotated);
  const Vector3 head_after = hierarchy.anchors[2]->GetDerivedPosition();
  Require(head_before != head_after,
          "import: rotating a joint did not move the anchors below it");
}


// reverse_team_processing swaps which team Match::Process() handles first
// (first_team = 1, second_team = 0) and shifts every frame toggle with it.
// Nothing else ran that configuration: every other check in this file uses the
// default order, so both the swapped processing order and its frame
// bookkeeping were entirely unexercised.
//
// This locks two properties, both observable inside a single run:
//
//   1. the canonical world frame is restored by every tick. Whichever team
//      was processed first, Match::Process() must return with the ball and
//      both teams unmirrored. The mirrored frame is a private mid-tick device
//      for the AI and collision code; it must never leak into state that a
//      caller can observe.
//   2. the configuration is self-reproducible: same seed, same scenario, same
//      tick count -> same observable state and the same simulation digest.
//
// It deliberately does NOT require reverse=true and reverse=false to follow
// the same trajectory. The processing order legitimately affects the legacy
// simulation, so that equality is not a real invariant and asserting it would
// encode an accident as a contract.
void CheckReverseTeamProcessing(GameEnv& env, ScenarioConfig& config) {
  const bool saved_reverse = config.reverse_team_processing;
  config.reverse_team_processing = true;

  // Phase 1: per-tick frame invariant.
  env.reset(config, false);
  Match* match = env.context->gameTask->GetMatch();
  Require(match->FirstTeam() == 1 && match->SecondTeam() == 0,
          "reverse_team_processing must process team 1 first");

  // Match::Process() is the simulation tick, and one env.step() is
  // physics_steps_per_frame of them. The per-tick invariant is therefore
  // checked by driving Process() directly: a leak that cancels inside a
  // single env.step() would be invisible at the step boundary.
  for (int i = 0; i < 600; ++i) {
    match->Process();
    const bool canonical = !match->isBallMirrored() &&
                           !match->GetTeam(0)->isMirrored() &&
                           !match->GetTeam(1)->isMirrored();
    if (!canonical) {
      std::ostringstream message;
      message << "reverse_team_processing: tick " << i
              << " did not restore the canonical frame (ball "
              << match->isBallMirrored() << ", team0 "
              << match->GetTeam(0)->isMirrored() << ", team1 "
              << match->GetTeam(1)->isMirrored() << ")";
      throw RegressionFailure(message.str());
    }
    Require(match->FirstTeam() == 1 && match->SecondTeam() == 0,
            "reverse_team_processing: processing order changed mid-run");
  }
  // Phase 2: reproducibility at env-step granularity.
  env.reset(config, false);
  Advance(env, 600);
  const SharedInfo reversed = env.get_info();
  const std::string reversed_digest = CaptureSimulationDigest(env);
  Require(reversed.is_in_play,
          "reverse_team_processing: the game never resumed");

  // Repeat from scratch. Unlike the plain reset check, the deep digest is
  // compared here too: this scenario reproduces exactly across resets, and a
  // digest is what catches damage the SharedInfo projection would hide.
  env.reset(config, false);
  Advance(env, 600);
  RequireInfoEqual(env.get_info(), reversed, "reverse_team_processing repeat");
  Require(CaptureSimulationDigest(env) == reversed_digest,
          "reverse_team_processing repeat: simulation digest differs");

  config.reverse_team_processing = saved_reverse;
}


// Formats a float as a valid C++ float literal, so that generated baseline
// entries can be pasted into the source without hand editing.
const char* FloatLiteral(float value) {
  if (value == 0.0f) return "0.0";  // avoid a noisy -0.0 in diffs
  static char buffers[9][64];
  static int next = 0;
  char* buffer = buffers[next];
  next = (next + 1) % 9;
  std::snprintf(buffer, 64, "%.9g", value);
  if (std::string(buffer).find_first_of(".eEnN") == std::string::npos) {
    std::strncat(buffer, ".0", 63 - std::strlen(buffer));
  }
  return buffer;
}

void PrintBaseline(GameEnv& env, ScenarioConfig& config) {
  const int calls[] = {1, 100, 500, 1000};
  env.reset(config, false);
  int completed = 0;
  std::cout << "  const GoldenSnapshot golden[] = {\n";
  for (const int target : calls) {
    Advance(env, target - completed);
    completed = target;
    const SharedInfo info = env.get_info();
    const Position& ball = info.ball_position;
    const Position& left = info.left_team.front().player_position;
    const Position& right = info.right_team.front().player_position;
    char line[1024];
    std::snprintf(
        line, sizeof(line),
        "      {%d, %d, Position(%sf, %sf, %sf, true),\n"
        "       Position(%sf, %sf, %sf, true),\n"
        "       Position(%sf, %sf, %sf, true), %d, %d, %s,\n"
        "       UINT64_C(%llu)},\n",
        target, info.step, FloatLiteral(ball.env_coord(0)),
        FloatLiteral(ball.env_coord(1)), FloatLiteral(ball.env_coord(2)),
        FloatLiteral(left.env_coord(0)), FloatLiteral(left.env_coord(1)),
        FloatLiteral(left.env_coord(2)), FloatLiteral(right.env_coord(0)),
        FloatLiteral(right.env_coord(1)), FloatLiteral(right.env_coord(2)),
        info.left_goals, info.right_goals,
        info.is_in_play ? "true" : "false",
        static_cast<unsigned long long>(HashInfo(info)));
    std::cout << line;
  }
  std::cout << "  };\n";
}

// The retained ball used to be positioned from a body-part transform read out
// of the skeletal animation tree, so its simulation position depended on when
// the presentation pipeline last refreshed that pose. H3a replaced that with a
// body-local anchor computed from simulation state.
//
// This checks the anchor semantics directly, then drives a real retain and
// requires the live ball to sit exactly on the computed anchor -- so the test
// cannot pass vacuously by never entering the retain path.
void CheckRetainAnchor(GameEnv& env, ScenarioConfig& config) {
  RetainAnchorKind kind = RetainAnchorKind::LeftElbow;
  Require(ParseRetainAnchorKind("right_elbow", kind) &&
              kind == RetainAnchorKind::RightElbow,
          "right_elbow must parse");
  Require(ParseRetainAnchorKind("left_elbow", kind) &&
              kind == RetainAnchorKind::LeftElbow,
          "left_elbow must parse");
  // Unknown anchors must not fall back silently: the legacy code asserted.
  Require(!ParseRetainAnchorKind("", kind), "empty retain state must not parse");
  Require(!ParseRetainAnchorKind("left_hand", kind),
          "unknown retain state must not parse");

  const Vector3 position(3.0f, -4.0f, 0.0f);
  const Vector3 forward(0.0f, -1.0f, 0.0f);
  const Vector3 rightAnchor =
      ComputeRetainAnchor(position, forward, RetainAnchorKind::RightElbow);
  const Vector3 leftAnchor =
      ComputeRetainAnchor(position, forward, RetainAnchorKind::LeftElbow);
  RequireNear(rightAnchor.coords[1], position.coords[1],
              "retain anchor should not push the ball forward");
  RequireNear(rightAnchor.coords[2], 1.26f, "retain anchor height");
  RequireNear(leftAnchor.coords[2], 1.26f, "retain anchor height (left)");
  Require(rightAnchor.coords[0] < position.coords[0] &&
              leftAnchor.coords[0] > position.coords[0],
          "left and right elbows must sit on opposite sides of the body");

  env.reset(config, false);
  for (int i = 0; i < 40; ++i) env.step();

  Match* match = env.context->gameTask->GetMatch();
  std::vector<Player*> players;
  match->GetActiveTeamPlayers(match->FirstTeam(), players);
  Require(!players.empty(), "retain scenario: no active player");
  Player* retainer = players.front();

  // The retain chain can be left by the controller's own animation selection,
  // so the retaining animation is re-asserted each tick to keep the path live.
  bool anchorObserved = false;
  for (int i = 0; i < 60; ++i) {
    retainer->SelectRetainAnim();
    match->SetBallRetainer(retainer);
    env.step();
    const Vector3 expectedAnchor =
        ComputeRetainAnchor(retainer->GetPosition(),
                            retainer->GetBodyDirectionVec(),
                            RetainAnchorKind::RightElbow);
    if (match->GetBall()->Predict(0).GetDistance(expectedAnchor) < 1e-3f) {
      anchorObserved = true;
    }
  }
  Require(anchorObserved,
          "retain scenario: the retain anchor path never ran, the test would "
          "be vacuous");
}


int main(int argc, char** argv) {
  if (!std::getenv("GFOOTBALL_DATA_DIR")) {
    std::cerr << "Set GFOOTBALL_DATA_DIR before running football_regression.\n";
    return 2;
  }

  try {

    CheckPlayerKinematics();
    CheckPlayerKinematicMirror();
    CheckProceduralLocomotion();
    CheckPlayerGroundCollider();
    CheckPlayerActionExecutor();
    CheckPureLocomotionBoundary();
    CheckLegacyLocomotionCommandAdapter();
    CheckPlayerActionVolume();
    CheckPlayerBodyCollider();
    GameEnv env;
    env.game_config.render = false;
    env.start_game();
    ScenarioConfig config = MakeBuiltinAiConfig();

    // Regenerate the golden baseline deliberately and reproducibly instead of
    // hand-editing the snapshot table. Run with --print-baseline and paste the
    // output over the `golden` array in CheckGoldenSnapshots.
    if (argc > 1 && std::string(argv[1]) == "--print-baseline") {
      PrintBaseline(env, config);
      return 0;
    }

    // Officials are placed in the Officials constructor, before any simulation
    // tick runs. Check the mirror here so that a placement path that bypasses
    // the synchronized reset is caught (the first Process would silently heal
    // it later).
    CheckKinematicMirrorConsistency(env, "kinematic mirror after start_game");
    CheckResetBitDeterminism(env, config);

    CheckGoldenSnapshots(env, config);
    CheckImportHierarchy();
    CheckRetainAnchor(env, config);
    CheckResetAndStateRoundTrip(env, config);
    CheckMovementAuthorityTiming(env, config);
    MeasureProceduralLocomotionDivergence(env, config);
    MeasureLocomotionRegimeTransitions(env, config);
    CheckMatchTransitions(env, config);
    CheckReverseTeamProcessing(env, config);
    std::cout << "football_regression: PASS\n";
    return 0;
  } catch (const RegressionFailure& failure) {
    std::cerr << "football_regression: FAIL: " << failure.what() << '\n';
    return 1;
  }
}
