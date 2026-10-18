// ANIM-1 A9.9: Player-side touch/animation math helpers, relocated from
// humanoid_utils.cpp. Bodies are verbatim moves; only the owning include and
// the unused NeedDefendingMovement helper (zero callers) changed.
#include "player/player_touch_math.hpp"

#include <cmath>

#include "football/animation/clip.hpp"
#include "football/animation/types.hpp"

#include "player/player_touch_query.hpp"
#include "player/player.hpp"
#include "player/player_motion_constants.hpp"

using football::ball::Ball;

e_TouchType GetTouchTypeForBodyPart(const std::string &bodypartname) {
  if (bodypartname.find("foot") != std::string::npos ||
      bodypartname.find("lowerleg") != std::string::npos)
    return e_TouchType_Intentional_Kicked;
  else
    return e_TouchType_Intentional_Nonkicked;
}

float CalculateBiasForFastCornering(const Vector3 &currentMovement,
                                    const Vector3 &desiredMovement,
                                    float veloPow, float bias) {
  // ANIM-2 A10.3: one pure definition, in the Animation module.
  return football::animation::CorneringBiasForFastCornering(
      currentMovement, desiredMovement, veloPow, bias);
}


// offset where ball should usually be touched
// ANIM-1 A6.4 step 2b: one definition, in the Animation module.
Vector3 GetFrontOfFootOffsetRel(float velocity, radian bodyAngleRel,
                                float height) {
  return football::animation::FrontOfFootOffsetRel(
      velocity, bodyAngleRel, height, static_cast<float>(defaultTouchOffset_ms));
}
void GetDifficultyFactors(const Ball *ball, Player *player,
                          const football::player::PlayerTouchFacts& touches, const PlayerTeamView &opponent,
                          football::sim::Tick now, football::math::Rng& rng,
                          const SpatialState &spatialState,
                          const Vector3 &positionOffset, float &distanceFactor,
                          float &heightFactor, float &ballMovementFactor) {


  distanceFactor = 0.0f; // how far ball bounces off feet
  heightFactor = 0.0f; // how high ball bounces off feet
  ballMovementFactor = 0.0f; // how much of the current ball movement is maintained
  // being pushed is tough
  float positionOffsetPenalty = NormalizedClamp(positionOffset.GetLength(), 0.0f, 0.1f) * 2.0f; // was: 7.0f
  // fast balls are harder
  float ballBodyVeloPenalty =
      std::pow(NormalizedClamp(
                   (spatialState.movement - ball->state().velocity).GetLength(),
                   10.0f, 50.0f),
               1.5f) *
      5.0f;
  // balls farther away from body are harder
  float fartherAwayPenalty =
      std::pow(NormalizedClamp(
                   ((spatialState.position + spatialState.directionVec * 0.2f) -
                    ball->state().position.Get2D())
                       .GetLength(),
                   0.7f, 1.3f),
               2.0f) *
      2.0f;

  distanceFactor += positionOffsetPenalty * 2.0f;
  distanceFactor += ballBodyVeloPenalty;
  distanceFactor += fartherAwayPenalty * 4.0f;

  heightFactor += positionOffsetPenalty * 0.5f;
  heightFactor += ballBodyVeloPenalty * 2.0f;
  heightFactor += fartherAwayPenalty;

  // make intercepting passes harder
  if (touches.last_team != player->GetTeamID()) {
    Player *lastTouchPlayer = football::sim::player::LastTouchPlayer(
        touches, opponent.team_id, opponent.teammates);
    if (lastTouchPlayer) {
      float lastTouchBiasPenalty =
          std::pow(lastTouchPlayer->GetLastTouchBias(
                       1000 - player->GetStat(football::player::PlayerStat::physical_reaction) * 500, now),
                   0.6f) *
          5.0f;
      distanceFactor += lastTouchBiasPenalty;
      heightFactor += lastTouchBiasPenalty;
      ballMovementFactor += lastTouchBiasPenalty * 0.1f;
    }
  }
  ballMovementFactor = clamp(ballMovementFactor, 0.0f, 0.9f);

  float skillPenaltyMultiplier =
      (1.0f - player->GetStat(football::player::PlayerStat::technical_ballcontrol) * 0.5f) *
      rng.Uniform(0.5f, 1.0f);
  distanceFactor *= skillPenaltyMultiplier;
  heightFactor *= skillPenaltyMultiplier;
  ballMovementFactor *= skillPenaltyMultiplier;
  distanceFactor = clamp(distanceFactor, 0.0f, 1.0f);
  heightFactor = clamp(heightFactor, 0.0f, 1.0f);
  ballMovementFactor = clamp(ballMovementFactor, 0.0f, 1.0f);
}

Vector3 GetBallControlVector(const Ball *ball, Player *player, const PlayerTeamView &opponent, const AnimationClip& clip,
                             const Vector3 &nextStartPos, radian nextStartAngle,
                             radian nextBodyAngle,
                             const Vector3 &outgoingMovement,
                             const Anim &currentAnim, int frameNum,
                             const SpatialState &spatialState,
                             const Vector3 &positionOffset, radian &xRot,
                             radian &yRot, float ffoOffset) {

  // part of the resulting direction is physics, the other part is anim/controller. so originatingBias is only applied on the (1.0 - physicsBias) part of the result
  float physicsBias = 0.7f;
  if (!player->HasPossession()) {
    physicsBias = 0.9f;
  }

  if (FloatToEnumVelocity(clip.metadata.outgoing_velocity) == e_Velocity_Idle) physicsBias = 1.0f;

  float explosivenessFactor = 0.6f;
  float maximumOverdrive_mps = 1.0f * explosivenessFactor;
  if (currentAnim.originatingCommand.desiredVelocityFloat < dribbleWalkSwitch) maximumOverdrive_mps = 0.0f;
  if (currentAnim.originatingCommand.desiredVelocityFloat > walkSprintSwitch) maximumOverdrive_mps = 2.0f * explosivenessFactor;
  float dotFactor1 = spatialState.directionVec.GetDotProduct(currentAnim.originatingCommand.desiredDirection) * 0.5f + 0.5f; // inv deviation from the current direction
  float dotFactor2 = Vector3(0, -1, 0).GetRotated2D(nextStartAngle).GetDotProduct(currentAnim.originatingCommand.desiredDirection) * 0.5f + 0.5f; // inv deviation from the direction we wanted
  float dotFactor = clamp(std::min(dotFactor1, dotFactor2), 0.0f, 1.0f);  // clamp for numerical problems, don't want negative values out of this.
  maximumOverdrive_mps *= dotFactor;

  if (FloatToEnumVelocity(clip.metadata.outgoing_velocity) == e_Velocity_Idle) maximumOverdrive_mps = 0.0f;

  float originatingBias = 0.7f;

  // using just controller velo
  Vector3 desiredMovement = currentAnim.originatingCommand.desiredDirection * (currentAnim.originatingCommand.desiredVelocityFloat * originatingBias + player->GetControlSpeed() * (1.0f - originatingBias));

  // range velocity, pes6 style
  //desiredMovement = desiredMovement.GetNormalized(0) * RangeVelocity(desiredMovement.GetLength());

  // hold velo to max player velo - else slow players will kick the ball too far away, which is sad
  if (desiredMovement.GetLength() > walkSprintSwitch) desiredMovement = desiredMovement.GetNormalized(0) * StretchSprintTo(desiredMovement.GetLength(), sprintVelocity, player->GetMaxVelocity());

  // don't want to go below physics velo, else we will run over ball and stuff like that
  // don't want to go over physics velo too much either, else ball will be so far away!
  float velocity = clamp(desiredMovement.GetLength(), outgoingMovement.GetLength(), outgoingMovement.GetLength() + maximumOverdrive_mps);
  desiredMovement = desiredMovement.GetNormalized(Vector3(0, -1, 0).GetRotated2D(nextStartAngle)) * velocity;
  Vector3 physicsMovement = Vector3(0, -1, 0).GetRotated2D(nextStartAngle) * velocity;

  float desiredVelocity = desiredMovement.GetLength();
  float physicsVelocity = physicsMovement.GetLength();

  Vector3 FFOsrc = GetFrontOfFootOffsetRel(physicsVelocity, nextBodyAngle - spatialState.angle, ball->state().position.coords[2]);
  float annoyanceVeloFactor = curve(NormalizedClamp(clip.metadata.outgoing_velocity, idleVelocity, sprintVelocity), 0.7f); // do not apply effect to low velo's; makes it too chaotic
  float opponentAnnoyanceFactor = (1.0f - NormalizedClamp(player->GetClosestOpponentDistance(opponent), 0.5f, 1.5f)) * (1.0f - (player->GetStat(football::player::PlayerStat::mental_calmness) * 0.5f + player->GetStat(football::player::PlayerStat::physical_balance) * 0.3f)) * annoyanceVeloFactor;
  Vector3 FFO = Vector3(0, -1, 0).GetRotated2D(nextBodyAngle) * (FFOsrc.GetLength() + ffoOffset + opponentAnnoyanceFactor * 3.0f); // positionOffset is already in ffoOffset (though only for trap atm)
  float heightFFOOffset = NormalizedClamp(ball->state().position.coords[2], 0.5f, 1.0f) * 0.5f; // bounce high balls off body - else they keep colliding inside body and stuff like that
  FFO += FFOsrc * heightFFOOffset * 0.5f +
         spatialState.bodyDirectionVec * heightFFOOffset * 0.5f;

  Vector3 desiredPlannedBallPos = nextStartPos;
  Vector3 physicsPlannedBallPos = nextStartPos;

  // we actually want the ball to be further away than our next position - after all, we want to hit it again a few steps later
  float desiredDelayTime =
      std::pow(NormalizedClamp(desiredVelocity, 0.0f, sprintVelocity), 2.0f) *
          0.60f +
      0.25f;  // how many seconds later do we want to be able to hit it again?
              // (remember: this is minus ffo time and current anim time)
  float physicsDelayTime =
      std::pow(NormalizedClamp(physicsVelocity, 0.0f, sprintVelocity), 2.0f) *
          0.60f +
      0.25f;  // how many seconds later do we want to be able to hit it again?
              // (remember: this is minus ffo time and current anim time)
  // printf("PHYSICS VELO: %f (desiredDelayTime: %f, physicsDelayTime: %f)\n",
  // physicsVelocity, desiredDelayTime, physicsDelayTime);
  desiredPlannedBallPos += desiredMovement * desiredDelayTime + FFO;
  physicsPlannedBallPos += physicsMovement * physicsDelayTime + FFO;

  /*
  if (player->GetDebug()) {
     SetGreenDebugPilon(nextStartPos);
     SetRedDebugPilon(nextStartPos + FFO);
     //SetGreenDebugPilon(desiredPlannedBallPos);
     SetYellowDebugPilon(physicsPlannedBallPos);
  }*/

  Vector3 plannedBallPos = physicsPlannedBallPos * physicsBias + desiredPlannedBallPos * (1.0f - physicsBias);
  Vector3 toPlannedBall = plannedBallPos - ball->state().position.Get2D();

  float timeToGo = ((static_cast<int>(clip.frame_count) - 1 - frameNum) * 10) * 0.001f;
  timeToGo += physicsDelayTime * physicsBias + desiredDelayTime * (1.0f - physicsBias);
  timeToGo += defaultTouchOffset_ms * 0.001f;//0.08f; // time into next anim where we want to hit the ball

  float divisor = timeToGo * (0.38f + 0.02f * player->GetStat(football::player::PlayerStat::technical_dribble)); // higher == closer
  divisor *= 1.1f;

  // to get the ball to the planned position in timeToGo seconds, we need to do some std::pow() magic, since the ball also slows down faster at higher ball velos
  float power = std::pow((toPlannedBall.GetLength() / divisor), 0.7f);
  Vector3 direction = toPlannedBall.GetNormalized(Vector3(0, -1, 0).GetRotated2D(nextBodyAngle));

  // SetYellowDebugPilon(nextStartPos);
  // SetGreenDebugPilon(nextStartPos + FFO);

  //if (player->GetDebug()) printf("power: %f\n", power);
  float height = clamp(0.1f + 1.5f * std::pow(power / 10.0f, 1.6f), 0.0f,
                       1.5f);  // power ~= 0 to 10

  //if (player->GetDebug()) printf("tech ballctrl: %f\n", player->GetStat(football::player::PlayerStat::technical_ballcontrol));
  float powerMultiplier = 1.2f - (player->GetStat(football::player::PlayerStat::technical_ballcontrol) * 0.03f); // 1.24 .. * 0.1
  float veloBias = NormalizedClamp(velocity, walkVelocity, sprintVelocity - 0.8f); // this multiplier only applies to high velocities
  powerMultiplier = 1.0f * (1.0f - veloBias) + powerMultiplier * veloBias;

  Vector3 touchVec = direction * power * powerMultiplier + Vector3(0, 0, height);
  /*
    if (player->GetDebug()) {
      SetRedDebugPilon(ball->state().position.Get2D());
      SetGreenDebugPilon(plannedBallPos);
    }
  */

  float backspinFactor = 30.0f;
  float velo = touchVec.GetLength();
  float veloFactor =
      std::pow(NormalizedClamp(touchVec.GetLength(), 0.0f, 10.0f),
               0.7f);  // extra around walk velocity
  xRot = (FFO.GetNormalized(0) * 0.6f + touchVec.GetNormalized(0) * 0.4f).GetNormalized(0).coords[1] * ((2 * pi * velo) - (backspinFactor * veloFactor));
  yRot = (FFO.GetNormalized(0) * 0.6f + touchVec.GetNormalized(0) * 0.4f).GetNormalized(0).coords[0] * ((2 * pi * velo) - (backspinFactor * veloFactor));

  //if (player->GetDebug()) printf("x: %f, y: %f\n", xRot, yRot);

  return touchVec;
}

Vector3 GetTrapVector(const Ball *ball, Player *player,
                      const football::player::PlayerTouchFacts& touches, const PlayerTeamView &opponent,
                      football::sim::Tick now, football::math::Rng& rng, const AnimationClip& clip,
                      const Vector3 &nextStartPos,
                      radian nextStartAngle, radian nextBodyAngle,
                      const Vector3 &outgoingMovement, const Anim &currentAnim,
                      int frameNum, const SpatialState &spatialState,
                      const Vector3 &positionOffset, radian &xRot,
                      radian &yRot) {


  float distanceFactor = 0.0f; // how far ball bounces off feet
  float heightFactor = 0.0f; // how high ball bounces off feet
  float ballMovementFactor = 0.0f; // how much of the current ball movement is maintained
  GetDifficultyFactors(ball, player, touches, opponent, now, rng, spatialState, positionOffset, distanceFactor, heightFactor, ballMovementFactor);

  // base vector to start with
  Vector3 ballControl = GetBallControlVector(ball, player, opponent, clip, nextStartPos, nextStartAngle, nextBodyAngle, outgoingMovement, currentAnim, frameNum, spatialState, positionOffset, xRot, yRot, distanceFactor);

  ballControl.coords[2] += ball->state().velocity.coords[2] * heightFactor * 1.0f +
                           heightFactor * 4.0f;

  return ballControl * (1.0f - ballMovementFactor) +
         ball->state().velocity * ballMovementFactor;
}

Vector3 GetShotVector(const Ball *ball, Player *player, const AnimationClip& clip, football::math::Rng& rng,
                      const Anim &currentAnim, const SpatialState &spatialState,
                      const Vector3 &positionOffset, radian &xRot, radian &yRot, radian &zRot) {

  const std::vector<Vector3> &origPositionCache = clip.root_positions;
  Vector3 touchMovement = CalculateMovementAtFrame(origPositionCache, currentAnim.frameNum).GetRotated2D(spatialState.angle); // spatialState.movement isn't reliable because of smuggles and such
  //SetRedDebugPilon(player->GetPosition() + touchMovement);
  Vector3 touchDirection = touchMovement.GetNormalized(spatialState.directionVec);
  float touchVelocity = touchMovement.GetLength();
  if (FloatToEnumVelocity(touchVelocity) == e_Velocity_Idle) touchDirection = spatialState.directionVec;
  //SetBlueDebugPilon(player->GetPosition() + touchDirection * 8.0f);

  float expressionModifierFactor = NormalizedClamp(currentAnim.originatingCommand.desiredVelocityFloat, idleVelocity, sprintVelocity);

  // previous: float directionFactor = touchDirection.GetDotProduct(currentAnim.originatingCommand.touchInfo.desiredDirection) * 0.5f + 0.5f;
  // ideal angle is ~36 degrees == 0.2 * pi
  radian idealAngle = 0.2f * pi;
  radian angle1 = std::fabs(touchDirection.GetAngle2D(currentAnim.originatingCommand.touchInfo.desiredDirection.GetRotated2D(-idealAngle)));
  radian angle2 = std::fabs(touchDirection.GetAngle2D(currentAnim.originatingCommand.touchInfo.desiredDirection.GetRotated2D( idealAngle)));
  radian angle = std::min(angle1, angle2);
  float directionFactor = clamp(angle / pi, 0.0f, 1.0f);
  directionFactor = curve(1.0f - directionFactor, 0.8f);
  //printf("directionfactor: %f\n", directionFactor);


  // calculate power. take desired power as maximum, and subtract based on player/ball movement and such

  float playerDirDesiredDirPowerFactor =
      std::pow(directionFactor,
               0.8f);  // shooting sideways is still pretty easy to power up

  float playerVelocityPowerFactor =
      std::pow(NormalizedClamp(touchVelocity, 0.0f, sprintVelocity),
               0.3f);  // >= walk velo is optimum

  float positionOffsetPowerFactor = 1.0f - NormalizedClamp(positionOffset.GetLength(), 0.0f, 0.1f);

  float powerFactor = 1.0f *
                      (0.7f + playerDirDesiredDirPowerFactor * 0.3f) *
                      (0.7f + playerVelocityPowerFactor * 0.3f) *
                      (0.4f + positionOffsetPowerFactor * 0.6f);
  powerFactor = clamp(powerFactor, 0.0f, 1.0f);

  float adaptedDesiredPower =
      45.0f *
      (0.7f +
       std::pow(currentAnim.originatingCommand.touchInfo.desiredPower, 0.5f) *
           0.3f);

  float animMaxPowerFactor = clip.metadata.touch_max_power_factor;
  if (animMaxPowerFactor == 0.0f) animMaxPowerFactor = 1.0f;

  float power = clamp(powerFactor * adaptedDesiredPower, 0.0f, (32.0f + player->GetStat(football::player::PlayerStat::physical_shotpower) * 13.0f) * (0.2f + animMaxPowerFactor * 0.8f));

  // add this after previous stat-clamp, because using the current ball movement is like an added (power) bonus that everybody profits from, even sucky players
  float playerMovBallMovPowerFactor = (touchMovement - ball->state().velocity).GetLength();
  playerMovBallMovPowerFactor = NormalizedClamp(playerMovBallMovPowerFactor, 0.0f, 10.0f);

  power *= 1.0f + playerMovBallMovPowerFactor * 0.2f;



  // calculate difficulty, based on factors like desired power, player/ball movement, skill, positionoffset etcetera

  float playerDirDesiredDirEasinessFactor = std::pow(
      directionFactor, 0.8f);  // shooting sideways is still pretty easy to aim

  float playerVelocityEasinessFactor = 1.0f - NormalizedClamp(std::fabs(touchVelocity - dribbleVelocity), 0.0f, 4.0f); // dribble velo is optimum

  float positionOffsetEasinessFactor = 1.0f - NormalizedClamp(positionOffset.GetLength(), 0.0f, 0.1f);

  float playerMovBallMovEasinessFactor = (touchMovement - ball->state().velocity).GetLength();
  playerMovBallMovEasinessFactor = 1.0f - playerMovBallMovPowerFactor * (0.5f - player->GetStat(football::player::PlayerStat::technical_volley) * 0.3f);

  float powerEasinessFactor = 1.0f - NormalizedClamp(power, 30.0f, 100.0f);
  powerEasinessFactor = curve(powerEasinessFactor, 1.0f);

  float easinessFactor = 1.0f *
                         (0.4f + playerDirDesiredDirEasinessFactor * 0.6f) *
                         (0.7f + playerVelocityEasinessFactor * 0.3f) *
                         (0.3f + positionOffsetEasinessFactor * 0.7f) *
                         (0.6f + playerMovBallMovEasinessFactor * 0.4f) *
                         (0.6f + powerEasinessFactor * 0.4f);
  float difficultyFactor = clamp(1.0f - easinessFactor, 0.0f, 1.0f);

  // best case result

  float desiredHeight = 0.05f;
  Vector3 desiredShot = (currentAnim.originatingCommand.touchInfo.desiredDirection.Get2D() + Vector3(0, 0, desiredHeight)).GetNormalized() * power;

  // worst case result

  Vector3 worstCaseDirection = currentAnim.originatingCommand.touchInfo.desiredDirection.Get2D();

  // direction lag
  float laggyDirectionBias = difficultyFactor * 0.8f;
  worstCaseDirection = (touchDirection * laggyDirectionBias + worstCaseDirection * (1.0f - laggyDirectionBias)).GetNormalized(0);

  // random dir
  worstCaseDirection =
      worstCaseDirection +
      (Vector3(rng.Uniform(-1, 1), rng.Uniform(-1, 1), rng.Uniform(-1, 1)) *
       0.5f * difficultyFactor);
  worstCaseDirection.Normalize();

  float worstCaseHeight = curve(std::pow(difficultyFactor, 0.7f), 0.7f) * 0.7f;

  float worstCasePower =
      power * (1.0f - std::pow(difficultyFactor, 0.7f) * 0.5f);

  Vector3 worstCaseShot = (worstCaseDirection + Vector3(0, 0, worstCaseHeight)).GetNormalized() * worstCasePower;

  // actual result

  float worstCaseFactor = rng.Uniform(0.0f, 1.0f);
  worstCaseFactor =
      std::pow(worstCaseFactor, player->GetStat(football::player::PlayerStat::technical_shot) * 0.7f);

  Vector3 shot = desiredShot * (1.0f - worstCaseFactor) +
                 worstCaseShot * worstCaseFactor;


  // add a little curve

  float randomCurveFactor = 0.3f + worstCaseFactor * 0.7f;
  float plannedCurveFactor = 0.7f;

  // forward/backward 'curve'
  xRot = -currentAnim.originatingCommand.touchInfo.desiredDirection.coords[1] *
             20.0f +
         (rng.Uniform(-20, 20) * randomCurveFactor);
  yRot = -currentAnim.originatingCommand.touchInfo.desiredDirection.coords[0] *
             20.0f +
         (rng.Uniform(-20, 20) * randomCurveFactor);

  // lateral curve
  radian bodyTouchAngle = spatialState.bodyDirectionVec.GetAngle2D(shot) / pi;
  if (std::fabs(bodyTouchAngle) > 0.5f) bodyTouchAngle = (1.0f - std::fabs(bodyTouchAngle)) * signSide(bodyTouchAngle);
  bodyTouchAngle *= 2.0f;
  //printf("bodyTouchAngle: %f\n", bodyTouchAngle);
  radian amount = bodyTouchAngle * 0.25f;
  shot.Rotate2D(amount * (0.4f + 0.6f * NormalizedClamp(shot.GetLength(), 0.0f, 70.0f)));
  zRot = amount * -420 + (rng.Uniform(-20, 20) * plannedCurveFactor);


  return shot;
}
