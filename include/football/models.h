#ifndef FOOTBALL_MODELS_H
#define FOOTBALL_MODELS_H

/* Shared C/C++ model definitions: caller-owned, unchecked values in SI units.
 * Fields are sequential 32-bit floats with native alignment, no custom packing.
 * These ABI layouts must not be extended in place.
 * Callers initialize every field explicitly; zeros are not physical defaults. */
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

/* C++ convenience only: aliases, not distinct types. */
#ifdef __cplusplus
namespace football {
namespace pitch {
using PitchModel = ::football_pitch_model;
}
namespace ball {
using BallModel = ::football_ball_model;
}
}  // namespace football
#endif

#endif