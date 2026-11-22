#ifndef FOOTBALL_MODELS_H
#define FOOTBALL_MODELS_H

typedef struct football_pitch_model {
  float length;
  float width;
  float quadratic_resistance;
  float ground_deceleration;
  float air_drag;
} football_pitch_model;

typedef struct football_ball_model {
  float mass;
  float radius;
  float magnus_coefficient;
} football_ball_model;

#ifdef __cplusplus
namespace football {
namespace pitch {
using PitchModel = ::football_pitch_model;
}
namespace ball {
using BallModel = ::football_ball_model;
}
}
#endif

#endif