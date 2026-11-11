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

/* Fixed-width status for C and P/Invoke. Zero means success. */
typedef int32_t football_status;
#define FOOTBALL_OK ((football_status)0)
#define FOOTBALL_INVALID_ARGUMENT ((football_status)1)
#define FOOTBALL_OUT_OF_MEMORY ((football_status)2)
#define FOOTBALL_INTERNAL_ERROR ((football_status)3)

/* The library owns exactly one environment. Models are copied: callers may
 * modify or discard them after football_init returns. Initializing twice fails
 * until football_shutdown is called; shutdown is safe to call at any time. */
FOOTBALL_API football_status FOOTBALL_CALL football_init(
    const football_pitch_model* pitch_model,
    const football_ball_model* ball_model) FOOTBALL_NOEXCEPT;
FOOTBALL_API void FOOTBALL_CALL football_shutdown(void) FOOTBALL_NOEXCEPT;

/* Thread-local UTF-8 diagnostic, empty after a successful status-returning call.
 * Valid until the next status-returning call on this thread. Do not free.
 * Copy immediately (C#: Marshal.PtrToStringUTF8). Messages may be truncated. */
FOOTBALL_API const char* FOOTBALL_CALL football_last_error(void) FOOTBALL_NOEXCEPT;

#ifdef __cplusplus
}
#endif

#endif
