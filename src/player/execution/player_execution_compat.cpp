// ANIM-1 A9.5-A9.8: the legacy compatibility projection and the locomotion
// bridge, relocated from Humanoid/HumanoidBase and then from Player.
//
// The bodies are verbatim moves; only the receiver and the access to the
// Player-owned facts changed. Nothing here is a second movement or action
// authority: `state_.legacy_spatial_state_` is a projection of the committed
// PlayerKinematicState, written at the mutation point.
#include "player/execution/player_execution.hpp"

#include <cassert>
#include <cmath>
#include <cstring>
#include <functional>
#include <stdexcept>

#include "football/ball/ball.hpp"
#include "football/animation/library.hpp"
#include "football/animation/movement_math.hpp"
#include "football/animation/movement_selection.hpp"
#include "football/animation/selection_math.hpp"
#include "football/animation/selector.hpp"
#include "football/player/execution/player_motion.hpp"
#include "player/execution/player_action_arbitration.hpp"
#include "player/execution/player_action_motion.hpp"
#include "player/execution/player_control_builder.hpp"
#include "player/execution/player_motion_constants.hpp"
#include "player/execution/player_runtime_sink.hpp"
#include "player/execution/player_touch_math.hpp"
#include "player/player.hpp"

using namespace football::math;
using football::ball::Ball;

void PlayerExecution::ResetLegacyPosition(const Vector3 &newPos,
                                 const Vector3 &focusPos) {

  state_.startPos = newPos;
  state_.startAngle = FixAngle((focusPos - newPos).GetNormalized(Vector3(0, -1, 0)).GetAngle2D());
  state_.nextStartPos = state_.startPos;
  state_.nextStartAngle = state_.startAngle;
  state_.previousPosition2D = state_.startPos;

  state_.legacy_spatial_state_.position = state_.startPos;
  state_.legacy_spatial_state_.angle = state_.startAngle;
  state_.legacy_spatial_state_.directionVec = Vector3(0, -1, 0).GetRotated2D(state_.startAngle);
  state_.legacy_spatial_state_.floatVelocity = 0;

  state_.legacy_spatial_state_.movement = Vector(0);
  state_.legacy_spatial_state_.actionSmuggleMovement = Vector(0);
  state_.legacy_spatial_state_.movementSmuggleMovement = Vector(0);
  state_.legacy_spatial_state_.positionOffsetMovement = Vector(0);
  state_.legacy_spatial_state_.relBodyAngleNonquantized = 0;

  state_.legacy_spatial_state_.enumVelocity = e_Velocity_Idle;
  state_.legacy_spatial_state_.floatVelocity = 0;
  state_.legacy_spatial_state_.movement = Vector3(0);
  state_.legacy_spatial_state_.relBodyDirectionVec = Vector3(0, -1, 0);
  state_.legacy_spatial_state_.relBodyAngle = 0;
  // After a reset the body faces the focus direction, matching the locomotion
  // direction and the animation base angle (state_.startAngle). Otherwise the derived
  // GetFacing() (velocity=0 -> bodyFacing) would report world -Y instead of the
  // direction the reset actually asked for. relBodyDirectionVec/relBodyAngle
  // stay (0,-1,0)/0 because body and locomotion now agree.
  state_.legacy_spatial_state_.bodyDirectionVec = state_.legacy_spatial_state_.directionVec;
  state_.legacy_spatial_state_.bodyAngle = state_.legacy_spatial_state_.angle;
  state_.legacy_spatial_state_.foot = e_Foot_Right;

  const AnimationId idleAnimID = SelectIdleMovementAnim(
      animations_, state_.legacy_spatial_state_.position, state_.legacy_spatial_state_.angle,
      state_.legacy_spatial_state_.floatVelocity);
  state_.legacy_anim_state_.animationId = idleAnimID;
  state_.legacy_anim_state_.positions.clear();
  state_.legacy_anim_state_.positions = GetBakedClip(state_.legacy_anim_state_.animationId).root_positions;
  // Deterministic entry frame: a fresh reset always starts the idle clip at
  // its first legal frame instead of drawing a random start.
  state_.legacy_anim_state_.frameNum = 0;
  state_.legacy_anim_state_.touchFrame = -1;
  state_.legacy_anim_state_.actionSmuggle = Vector3(0);
  state_.legacy_anim_state_.actionSmuggleOffset = Vector3(0);
  state_.legacy_anim_state_.actionSmuggleSustain = Vector3(0);
  state_.legacy_anim_state_.actionSmuggleSustainOffset = Vector3(0);
  state_.legacy_anim_state_.movementSmuggle = Vector3(0);
  state_.legacy_anim_state_.movementSmuggleOffset = Vector3(0);
  state_.legacy_anim_state_.rotationSmuggle.begin = 0;
  state_.legacy_anim_state_.rotationSmuggle.end = 0;
  state_.legacy_anim_state_.rotationSmuggleOffset = 0;
  state_.legacy_anim_state_.functionType = e_FunctionType_Movement;
  state_.legacy_anim_state_.incomingMovement = Vector3(0);
  state_.legacy_anim_state_.outgoingMovement = Vector3(0);
  state_.legacy_anim_state_.positionOffset = Vector3(0);

  state_.previousAnim_functionType = e_FunctionType_Movement;



  state_.interruptAnim = e_InterruptAnim_None;
  state_.tripType = 0;

  state_.decayingPositionOffset = Vector3(0);
  state_.decayingDifficultyFactor = 0.0f;

  state_.reQueueDelayFrames = 0;
  state_.tripDirection = Vector3(0);
}

void PlayerExecution::ApplyLegacyPositionOffset(const Vector3 &offset) {

  assert(offset.coords[2] == 0.0f);

  state_.nextStartPos += offset;
  state_.startPos += offset;
  state_.legacy_spatial_state_.position += offset;
  state_.legacy_spatial_state_.positionOffsetMovement += offset * 100.0f;
  state_.decayingPositionOffset += offset;
  if (state_.decayingPositionOffset.GetLength() > 0.1f) state_.decayingPositionOffset = state_.decayingPositionOffset.GetNormalized() * 0.1f;
  state_.legacy_anim_state_.positionOffset += offset;
}



void PlayerExecution::ResetLegacySituation(const Vector3 &focusPos) {
  state_.mentalImageTime = std::chrono::milliseconds{0};
  ResetLegacyPosition(state_.legacy_spatial_state_.position, focusPos);
}

bool PlayerExecution::HighOrBouncyBall(const Ball& ball) const {
  float ballHeight1 = ball.Predict(football::sim::TickSpan{1}).position.coords[2];
  float ballHeight2 = ball.Predict(football::sim::TickSpan{defaultTouchOffset_ms / football::sim::kMillisecondsPerTick}).position.coords[2];
  float ballBounce = std::fabs(ball.state().velocity.coords[2]);
  bool highBall = false;
  if (ballHeight1 > 0.3f || ballHeight2 > 0.3f) {
    highBall = true;
  } else if (ballBounce > 1.0f) {
      // low balls are also treated as 'high ball' when there's a
                    // lot of bounce going on (hard to control)
    highBall = true;
  }
  return highBall;
}

// ALERT: set sorting predicates before calling this function

// ALERT: set sorting predicates before calling this function


void PlayerExecution::CalculatePredictedSituation(Vector3 &predictedPos,
                                               radian &predictedAngle) {

  if (state_.legacy_anim_state_.positions.size() > (unsigned int)state_.legacy_anim_state_.frameNum) {
    assert(state_.legacy_anim_state_.positions.size() > (unsigned int)(static_cast<int>(GetCurrentBakedClip().frame_count) - 1));
    predictedPos = state_.legacy_spatial_state_.position + state_.legacy_anim_state_.positions.at((static_cast<int>(GetCurrentBakedClip().frame_count) - 1)) + state_.legacy_anim_state_.actionSmuggle + state_.legacy_anim_state_.actionSmuggleSustain + state_.legacy_anim_state_.movementSmuggle;
  } else {
    predictedPos = state_.legacy_spatial_state_.position + GetCurrentBakedClip().metadata.translation.Get2D().GetRotated2D(state_.legacy_spatial_state_.angle) + state_.legacy_anim_state_.actionSmuggle + state_.legacy_anim_state_.actionSmuggleSustain + state_.legacy_anim_state_.movementSmuggle;
  }

  predictedAngle = state_.legacy_spatial_state_.angle + GetCurrentBakedClip().metadata.outgoing_angle + state_.legacy_anim_state_.rotationSmuggle.end;
  predictedAngle = ModulateIntoRange(-pi, pi, predictedAngle);
  assert(predictedPos.coords[2] == 0.0f);
}

Vector3 PlayerExecution::CalculateOutgoingMovement(const std::vector<Vector3> &positions) const {
  if (positions.size() < 2) return 0;
  return (positions.at(positions.size() - 1) - positions.at(positions.size() - 2)) * 100.0f;
}

bool PlayerExecution::UsesProceduralLocomotion(const Player* ball_retainer) const {
  // Execution authority is the Player Decision Clock's current-epoch intent, not
  // the animation command. Executability is the gate; the producer contract is a
  // separate fatal oracle.
  return IsEligibleForProceduralLocomotion(ball_retainer == self()) &&
         HasExecutableDecisionLocomotionIntent();
}


void PlayerExecution::CalculateSpatialState(const Player* ball_retainer) {
  if (UsesProceduralLocomotion(ball_retainer)) {
    // H3e4d2: root-motion and body-pose producers are dead on this path. Foot
    // remains animation-owned gait/selection state until its separate audit.
    if (state_.legacy_anim_state_.frameNum > 12) {
      state_.legacy_spatial_state_.foot = static_cast<e_Foot>(GetCurrentBakedClip().metadata.outgoing_foot);
    }
    return;
  }
  // ANIM-1 A8 step 4: the non-procedural (baked action) motion is computed and
  // committed by the Player. This only keeps the compatibility projection that
  // the Player writes for later same-tick readers.
  ComputeAndCommitActionMotion();
}


void PlayerExecution::ProjectSimulationMovementState(
    const PlayerKinematicState &state, const Player* ball_retainer) {
  state_.legacy_spatial_state_.position = state.position;
  state_.legacy_spatial_state_.movement = state.velocity;
  if (UsesProceduralLocomotion(ball_retainer)) {
    // H3e4d1: these serialized legacy aliases have no pure-locomotion producer.
    // Project the authoritative velocity rather than retaining animation history.
  }
  state_.legacy_spatial_state_.directionVec = state.GetFacing();
  state_.legacy_spatial_state_.floatVelocity = state_.legacy_spatial_state_.movement.GetLength();
  state_.legacy_spatial_state_.enumVelocity = FloatToEnumVelocity(state_.legacy_spatial_state_.floatVelocity);
  state_.legacy_spatial_state_.angle =
      ModulateIntoRange(-pi, pi, FixAngle(state_.legacy_spatial_state_.directionVec.GetAngle2D()));
  ApplySimulationBodyState(state);
  // The next tick derives its raw movement from self(), so it must describe the
  // position actually in force. Collision corrections land here too, exactly
  // as they did when the legacy root motion owned the position.
  state_.previousPosition2D = state_.legacy_spatial_state_.position;
}

void PlayerExecution::ApplySimulationMovementState(
    const PlayerKinematicState &state, const Player* ball_retainer) {
  // Legacy action path: animation root motion produced this state, so adopting
  // it as the Player's motion is correct here -- and is the only remaining
  // source-explicit entry that re-reads the compatibility projection.
  ProjectSimulationMovementState(state, ball_retainer);
  port().CommitLegacyActionMotion();
}

void PlayerExecution::ApplySimulationBodyState(
    const PlayerKinematicState &state) {
  // Continuous simulation truth. Nothing in this function writes back into
  // state.bodyFacing, so legacy quantization cannot corrupt torso authority.
  state_.legacy_spatial_state_.bodyDirectionVec = state.bodyFacing;
  state_.legacy_spatial_state_.bodyAngle =
      state_.legacy_spatial_state_.bodyDirectionVec.GetAngle2D(Vector3(0, -1, 0));
  const Vector3 relative = state.bodyFacing.GetRotated2D(-state_.legacy_spatial_state_.angle)
                               .GetNormalized(Vector3(0, -1, 0));
  state_.legacy_spatial_state_.relBodyAngleNonquantized =
      relative.GetAngle2D(Vector3(0, -1, 0));

  // Compatibility for animation selection only. This quantization is a leaf:
  // bodyDirectionVec remains the continuous simulation projection above.
  state_.legacy_spatial_state_.relBodyDirectionVec =
      ForceIntoAllowedBodyDirectionVec(relative);
  state_.legacy_spatial_state_.relBodyAngle =
      state_.legacy_spatial_state_.relBodyDirectionVec.GetAngle2D(Vector3(0, -1, 0));
}


void PlayerExecution::ProjectMovementState(
    const PlayerKinematicState &tickStartState, const Player* ball_retainer,
    bool restart_needs_simulation) {

  if (!UsesProceduralLocomotion(ball_retainer)) {
    // Non-locomotion ticks keep the legacy animation root motion. Projecting
    // it keeps the compatibility movement fields a single projection of the
    // simulation kinematic state.
    ApplySimulationMovementState(port().GetKinematicState(), ball_retainer);
    return;
  }

  // Pure locomotion: the simulation produces the movement and the legacy
  // compatibility movement fields follow it. The command and the starting state are
  // the tick-start ones, so an action selection later in this tick cannot
  // change what this tick's locomotion was. Writing state_.legacy_spatial_state_.position here
  // means any later action already starts from the authoritative position
  // instead of an animation one.
  // The adapter consumes only the tick-start simulation state. Locomotion
  // produces lower-body motion first; the independent torso model then uses the
  // resulting facing and the command's look target.
  // The tick that consumes the command must see an exact copy of it.
  // Pure locomotion: one command source, the Player Decision Clock. Both the
  // movement model and the torso model consume the same decision-owned intent,
  // so the animation command no longer decides what executes.
  CheckDecisionLocomotionIntentOracle();
  const PlayerCommand &command = GetDecisionLocomotionIntent();
  // P2a: Player computes the locomotion through PlayerMotion and commits the
  // resulting state. S0.1: this path only *projects* it into the legacy spatial
  // state -- it does not adopt it back, so the compatibility state can no longer overwrite
  // a locomotion tick the Player already owns. The oracle proves the two sides
  // agree at this point.
  const PlayerKinematicState next = port().CommitLocomotion(
      command, tickStartState, restart_needs_simulation, 0.01f);
  ProjectSimulationMovementState(next, ball_retainer);
  port().CheckKinematicOracle();
}













Vector3 PlayerExecution::CalculatePhysicsVector(football::sim::Tick now, int animID, bool useDesiredMovement, const Vector3 &desiredMovement, bool useDesiredBodyDirection, const Vector3 &desiredBodyDirectionRel, std::vector<Vector3> &positions_ret, radian &rotationOffset_ret) const {
  // ANIM-1 A6.4 A8.0: the candidate reference-motion kernel now lives in
  // kinematic/capability inputs and writes the plan back; the algorithm itself
  // is unchanged (see PlayerActionMotion::BuildActionMotionPlan).
  const AnimationClip &clip = GetBakedClip(animID);

  ActionMotionInput input;
  input.spatial_movement = state_.legacy_spatial_state_.movement;
  input.angle = state_.legacy_spatial_state_.angle;
  input.direction_vec = state_.legacy_spatial_state_.directionVec;
  input.float_velocity = state_.legacy_spatial_state_.floatVelocity;
  input.use_desired_movement = useDesiredMovement;
  input.desired_movement = desiredMovement;
  input.use_desired_body_direction = useDesiredBodyDirection;
  input.desired_body_direction_rel = desiredBodyDirectionRel;
  input.parameters.max_velocity = port().GetMaxVelocity();
  input.parameters.stat_agility = port().GetStat(football::player::PlayerStat::physical_agility);
  input.parameters.stat_acceleration = port().GetStat(football::player::PlayerStat::physical_acceleration);
  input.parameters.stat_velocity = port().GetStat(football::player::PlayerStat::physical_velocity);
  input.parameters.stat_dribble = port().GetStat(football::player::PlayerStat::technical_dribble);
  input.parameters.stat_balance = port().GetStat(football::player::PlayerStat::physical_balance);
  input.parameters.stat_technical_ballcontrol = port().GetStat(football::player::PlayerStat::technical_ballcontrol);
  input.parameters.last_touch_bias_1000 = port().GetLastTouchBias(1000, now);
  input.parameters.decaying_position_offset_length = state_.decayingPositionOffset.GetLength();

  const ActionMotionPlan plan = PlayerActionMotion::BuildActionMotionPlan(clip, input);

  positions_ret = plan.positions;
  rotationOffset_ret = plan.rotation_offset;
  return plan.outgoing_velocity;
}

