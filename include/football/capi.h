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
 * clock, so matches never share anything and run independently. */
typedef struct football_session football_session;

/* Creates a session paused at tick 0 with its loop thread parked. Models are
 * copied, so callers may modify or discard them afterwards. A match is always 90
 * minutes at 100 Hz (540000 ticks), with half time at 45. Returns NULL for a NULL
 * model or when the loop thread cannot start. */
FOOTBALL_API football_session* FOOTBALL_CALL football_session_create(
    const football_pitch_model* pitch_model,
    const football_ball_model* ball_model) FOOTBALL_NOEXCEPT;

/* Stops the loop, joins it, releases the session and writes every buffered
 * log entry. NULL is ignored; a handle must be destroyed exactly once,
 * before the process exits. */
FOOTBALL_API void FOOTBALL_CALL football_session_destroy(
    football_session* session) FOOTBALL_NOEXCEPT;

/* Control. Idempotent, safe from any thread, applied by the loop between ticks.
 * Resuming after full time does nothing: a match never restarts. */
FOOTBALL_API void FOOTBALL_CALL football_resume(
    football_session* session) FOOTBALL_NOEXCEPT;
FOOTBALL_API void FOOTBALL_CALL football_pause(
    football_session* session) FOOTBALL_NOEXCEPT;

/* Published progress: written by the loop, readable from any thread. A NULL
 * handle reports neither running nor finished. */
FOOTBALL_API int FOOTBALL_CALL football_running(
    const football_session* session) FOOTBALL_NOEXCEPT;
FOOTBALL_API int FOOTBALL_CALL football_finished(
    const football_session* session) FOOTBALL_NOEXCEPT;

#ifdef __cplusplus
}
#endif

#endif
