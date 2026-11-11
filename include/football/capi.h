#ifndef FOOTBALL_CAPI_H
#define FOOTBALL_CAPI_H

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

/* The library owns exactly one environment. Models are copied: callers may
 * modify or discard them after football_init returns. Calling football_init
 * again replaces the current environment; shutdown is safe at any time. */
FOOTBALL_API void FOOTBALL_CALL football_init(
    const football_pitch_model* pitch_model,
    const football_ball_model* ball_model) FOOTBALL_NOEXCEPT;
FOOTBALL_API void FOOTBALL_CALL football_shutdown(void) FOOTBALL_NOEXCEPT;

#ifdef __cplusplus
}
#endif

#endif
