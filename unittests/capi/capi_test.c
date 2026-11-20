#include "football/capi.h"

#include <stddef.h>
#include <string.h>

_Static_assert(sizeof(football_pitch_model) == 20, "pitch ABI size");
_Static_assert(sizeof(football_ball_model) == 12, "ball ABI size");
_Static_assert(offsetof(football_pitch_model, air_drag) == 16, "pitch ABI offset");
_Static_assert(offsetof(football_ball_model, magnus_coefficient) == 8, "ball ABI offset");

int main(void) {
  football_pitch_model pitch = {105.0f, 68.0f, 0.04f, 1.6f, 0.015f};
  football_ball_model ball = {0.43f, 0.11f, 0.02f};

  /* NULL handles are inert, and a NULL model is rejected instead of crashing. */
  football_session_destroy(NULL);
  if (football_session_create(NULL, &ball) != NULL) return 1;
  if (football_session_create(&pitch, NULL) != NULL) return 2;

  /* A handle is one match; two handles are two independent matches. */
  football_session* a = football_session_create(&pitch, &ball);
  football_session* b = football_session_create(&pitch, &ball);
  if (a == NULL || b == NULL) return 3;
  if (a == b) return 4;

  /* Models are copied on create, so zeroing the caller's structs is harmless. */
  memset(&pitch, 0, sizeof(pitch));
  memset(&ball, 0, sizeof(ball));
  football_session* c = football_session_create(&pitch, &ball);
  if (c == NULL) return 5;

  football_session_destroy(c);
  football_session_destroy(b);
  football_session_destroy(a);

  return 0;
}
