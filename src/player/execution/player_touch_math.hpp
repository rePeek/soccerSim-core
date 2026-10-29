// ANIM-1 A9.9: Player-side touch/animation math helpers, relocated from
// humanoid_utils. They read the Player-owned compatibility types (Anim,
// SpatialState) and a Player*, never a Humanoid.
#ifndef FOOTBALL_SIM_PLAYER_TOUCH_MATH_HPP
#define FOOTBALL_SIM_PLAYER_TOUCH_MATH_HPP

#include "football/animation/movement_math.hpp"
#include "football/animation/touch_geometry.hpp"

#include "football/ball/ball.hpp"
#include "football/foundation/math/vector3.hpp"

#include "football/player/execution/player_touch_facts.hpp"
#include "football/contact/player_touch_type.hpp"
#include "player/execution/player_action_execution_state.hpp"
#include "player/player_spatial_state.hpp"
#include "player/execution/player_team_view.hpp"

using namespace football::math;

class Player;
struct AnimationClip;

e_TouchType GetTouchTypeForBodyPart(const std::string &bodypartname);
float CalculateBiasForFastCornering(const Vector3 &currentMovement,
                                    const Vector3 &desiredMovement,
                                    float veloPow = 1.0f, float bias = 1.0f);
using football::animation::CalculateMovementAtFrame;
Vector3 GetFrontOfFootOffsetRel(float velocity, radian bodyAngleRel, float height);
using football::animation::StretchSprintTo;
void GetDifficultyFactors(const football::ball::Ball *ball, Player *player, const football::player::PlayerTouchFacts& touches, const PlayerTeamView &opponent, football::sim::Tick now, const SpatialState &spatialState, const Vector3 &positionOffset, float &distanceFactor, float &heightFactor, float &ballMovementFactor);
Vector3 GetBallControlVector(const football::ball::Ball *ball, Player *player, bool has_possession, const PlayerTeamView &opponent, const AnimationClip& clip, const Vector3 &nextStartPos, radian nextStartAngle, radian nextBodyAngle, const Vector3 &outgoingMovement, const Anim &currentAnim, int frameNum, const SpatialState &spatialState, const Vector3 &positionOffset, float ffoOffset = 0.0f);
Vector3 GetTrapVector(const football::ball::Ball *ball, Player *player, bool has_possession, const football::player::PlayerTouchFacts& touches, const PlayerTeamView &opponent, football::sim::Tick now, const AnimationClip& clip, const Vector3 &nextStartPos, radian nextStartAngle, radian nextBodyAngle, const Vector3 &outgoingMovement, const Anim &currentAnim, int frameNum, const SpatialState &spatialState, const Vector3 &positionOffset);
Vector3 GetShotVector(const football::ball::Ball *ball, Player *player, const AnimationClip& clip, const Anim &currentAnim, const SpatialState &spatialState, const Vector3 &positionOffset);

#endif  // FOOTBALL_SIM_PLAYER_TOUCH_MATH_HPP
