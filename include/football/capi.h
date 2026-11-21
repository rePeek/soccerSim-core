#ifndef FOOTBALL_CAPI_H
#define FOOTBALL_CAPI_H

#include <stdint.h>

#include "football/models.h"

#if defined(_WIN32)
#  if defined(FOOTBALL_CAPI_BUILD)
#    define FOOTBALL_API __declspec(dllexport)
#  else
#    define FOOTBALL_API __declspec(dllimport)
#  endif
#  define FOOTBALL_CALL __cdecl
#else
#  define FOOTBALL_API __attribute__((visibility("default")))
#  define FOOTBALL_CALL
#endif

#ifdef __cplusplus
#  define FOOTBALL_NOEXCEPT noexcept
extern "C" {
#else
#  define FOOTBALL_NOEXCEPT
#endif

typedef struct football_session football_session;

typedef struct football_render_snapshot {
  uint64_t tick;
  float px, py, pz;
  float qx, qy, qz, qw;
} football_render_snapshot;

FOOTBALL_API football_session* FOOTBALL_CALL football_session_create(
    const football_pitch_model* pitch_model,
    const football_ball_model* ball_model,
    uint64_t ticks_per_step) FOOTBALL_NOEXCEPT;

FOOTBALL_API void FOOTBALL_CALL football_session_start(
    football_session* session) FOOTBALL_NOEXCEPT;

FOOTBALL_API void FOOTBALL_CALL football_session_destroy(
    football_session* session) FOOTBALL_NOEXCEPT;

FOOTBALL_API int FOOTBALL_CALL football_get_render_snapshot(
    const football_session* session, uint64_t tick,
    football_render_snapshot* output) FOOTBALL_NOEXCEPT;

#ifdef __cplusplus
}
#endif

#endif
