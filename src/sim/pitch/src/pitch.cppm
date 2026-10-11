// football.pitch — C++20 named module that turns a model::Pitch description
// into Ball's static physical world (ground plane, posts, crossbars, ground
// material, net geometry). Ball never reads model::Pitch itself.
module;

#include "model/pitch.hpp"
#include "model/ball_config.hpp"
#include "football/ball/ball_world.hpp"

export module football.pitch;

export namespace football::pitch {

// Assembles the complete static BallWorld from the pitch description and the
// active ball material. Deterministic; constructs once per match.
football::ball::BallWorld BuildBallWorld(const football::model::Pitch& pitch,
                                         const football::model::BallConfig& ball);

}  // namespace football::pitch
