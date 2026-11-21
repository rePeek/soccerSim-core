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

/* Opaque handle: one environment whose loop runs on a thread of its own. Any
 * number of sessions may exist at the same time; each owns its state and its
 * match, so matches never share anything and run independently. */
typedef struct football_session football_session;

/* Creates a session and starts its loop on a thread of its own. Models are
 * copied, so callers may modify or discard them afterwards. A match is always 90
 * minutes at 100 Hz (540000 ticks), with half time at 45. Returns NULL for a NULL
 * model or when the loop thread cannot start. */
FOOTBALL_API football_session* FOOTBALL_CALL football_session_create(
    const football_pitch_model* pitch_model,
    const football_ball_model* ball_model) FOOTBALL_NOEXCEPT;

/* Stops the loop, joins it, releases the session and writes every buffered log
 * entry. NULL is ignored; a handle must be destroyed exactly once, before the
 * process exits. A match short of full time is stopped where it is. */
FOOTBALL_API void FOOTBALL_CALL football_session_destroy(
    football_session* session) FOOTBALL_NOEXCEPT;

#ifdef __cplusplus
}
#endif

#endif
