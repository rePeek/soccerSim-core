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

  /* Lifecycle; shutdown is idempotent. */
  football_init(&pitch, &ball);
  football_shutdown();
  football_shutdown();

  /* Re-init replaces the current environment; the model values are copied,
   * so zeroing the caller's structs afterwards is harmless. */
  football_init(&pitch, &ball);
  memset(&pitch, 0, sizeof(pitch));
  memset(&ball, 0, sizeof(ball));
  football_init(&pitch, &ball);
  football_shutdown();

  return 0;
}
