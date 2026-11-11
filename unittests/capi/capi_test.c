#include "football/capi.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(football_pitch_model) == 20, "pitch ABI size");
_Static_assert(sizeof(football_ball_model) == 12, "ball ABI size");
_Static_assert(offsetof(football_pitch_model, air_drag) == 16, "pitch ABI offset");
_Static_assert(offsetof(football_ball_model, magnus_coefficient) == 8, "ball ABI offset");

/* Unlike assert(), these checks also run in release builds. */
#define CHECK(expression) do { \
  if (!(expression)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
    return 1; \
  } \
} while (0)

int main(void) {
  football_pitch_model pitch = {105.0f, 68.0f, 0.04f, 1.6f, 0.015f};
  football_ball_model ball = {0.43f, 0.11f, 0.02f};
  CHECK(football_last_error()[0] == '\0');

  /* Lifecycle: init, then shutdown is idempotent. */
  CHECK(football_init(&pitch, &ball) == FOOTBALL_OK);
  CHECK(football_last_error()[0] == '\0');
  CHECK(football_init(&pitch, &ball) == FOOTBALL_INVALID_ARGUMENT);
  CHECK(football_last_error()[0] != '\0');
  football_shutdown();
  football_shutdown();

  CHECK(football_init(&pitch, &ball) == FOOTBALL_OK);
  football_shutdown();

  /* NULL arguments are rejected. */
  CHECK(football_init(NULL, &ball) == FOOTBALL_INVALID_ARGUMENT);
  CHECK(football_init(&pitch, NULL) == FOOTBALL_INVALID_ARGUMENT);

  /* Caller-authored models are accepted and copied, not retained. */
  pitch = (football_pitch_model){90.0f, 60.0f, 0.0f, 0.0f, 0.0f};
  ball = (football_ball_model){0.5f, 0.12f, 0.0f};
  CHECK(football_init(&pitch, &ball) == FOOTBALL_OK);
  memset(&pitch, 0, sizeof(pitch));
  memset(&ball, 0, sizeof(ball));
  football_shutdown();

  /* A successful status-returning call clears the last error. */
  CHECK(football_init(NULL, &ball) == FOOTBALL_INVALID_ARGUMENT);
  CHECK(football_last_error()[0] != '\0');
  CHECK(football_init(&pitch, &ball) == FOOTBALL_OK);
  CHECK(football_last_error()[0] == '\0');
  football_shutdown();
  return 0;
}
