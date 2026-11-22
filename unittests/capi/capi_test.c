#include "football/capi.h"

#include <stddef.h>
#include <string.h>

_Static_assert(sizeof(football_pitch_model) == 20, "pitch ABI size");
_Static_assert(sizeof(football_ball_model) == 12, "ball ABI size");
_Static_assert(offsetof(football_pitch_model, air_drag) == 16, "pitch ABI offset");
_Static_assert(offsetof(football_ball_model, magnus_coefficient) == 8, "ball ABI offset");
_Static_assert(sizeof(football_render_snapshot) == 40, "render snapshot ABI size");
_Static_assert(offsetof(football_render_snapshot, tick) == 0, "render tick offset");
_Static_assert(offsetof(football_render_snapshot, px) == 8, "render position offset");
_Static_assert(offsetof(football_render_snapshot, qx) == 20, "render orientation offset");
_Static_assert(offsetof(football_render_snapshot, qw) == 32, "render quaternion w offset");

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

  football_render_snapshot snapshot = {0};
  football_render_snapshot original = snapshot;
  if (football_get_render_snapshot(NULL, 0, &snapshot) != 0) return 6;
  if (memcmp(&snapshot, &original, sizeof(snapshot)) != 0) return 7;
  if (football_get_render_snapshot(a, 0, NULL) != 0) return 8;
  if (football_get_render_snapshot(a, UINT64_MAX, &snapshot) != 0) return 9;
  if (memcmp(&snapshot, &original, sizeof(snapshot)) != 0) return 10;

  /* Tick zero is recorded before the worker starts; reads need no wait. */
  if (football_get_render_snapshot(a, 0, &snapshot) != 1) return 11;
  if (snapshot.tick != 0 || snapshot.px != 0.0f || snapshot.py != 0.0f ||
      snapshot.pz != ball.radius || snapshot.qx != 0.0f ||
      snapshot.qy != 0.0f || snapshot.qz != 0.0f || snapshot.qw != 1.0f) return 12;
  snapshot.px = 123.0f;
  if (football_get_render_snapshot(a, 0, &snapshot) != 1 ||
      snapshot.px != 0.0f) return 13;
  if (football_get_render_snapshot(b, 0, &snapshot) != 1 ||
      snapshot.pz != ball.radius) return 14;

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
