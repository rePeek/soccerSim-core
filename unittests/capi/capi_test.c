#include "football/capi.h"

#include <stddef.h>
#include <stdio.h>
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

#define REQUIRE(expression) do { \
  if (!(expression)) { \
    fprintf(stderr, "%s:%d: %s: %s\n", __FILE__, __LINE__, __func__, #expression); \
    return 0; \
  } \
} while (0)

static football_pitch_model DefaultPitch(void) {
  return (football_pitch_model){.length = 105.0f, .width = 68.0f,
                                .quadratic_resistance = 0.04f,
                                .ground_deceleration = 1.6f, .air_drag = 0.015f};
}

static football_ball_model DefaultBall(void) {
  return (football_ball_model){.mass = 0.43f, .radius = 0.11f,
                               .magnus_coefficient = 0.02f};
}

static int TestNullArguments(void) {
  football_pitch_model pitch = DefaultPitch();
  football_ball_model ball = DefaultBall();
  football_render_snapshot snapshot = {0};
  football_render_snapshot original = snapshot;

  football_session_destroy(NULL);
  football_session_start(NULL);
  REQUIRE(football_session_create(NULL, &ball, NULL) == NULL);
  REQUIRE(football_session_create(&pitch, NULL, NULL) == NULL);
  REQUIRE(football_get_render_snapshot(NULL, 0, &snapshot) == 0);
  REQUIRE(memcmp(&snapshot, &original, sizeof(snapshot)) == 0);
  return 1;
}

static int TestSessionsHaveIndependentSnapshotCopies(void) {
  football_pitch_model pitch = DefaultPitch();
  football_ball_model first_ball = DefaultBall();
  football_ball_model second_ball = DefaultBall();
  second_ball.radius = 0.2f;
  football_session* first_session = football_session_create(&pitch, &first_ball, NULL);
  football_session* second_session = football_session_create(&pitch, &second_ball, NULL);
  REQUIRE(first_session != NULL);
  REQUIRE(second_session != NULL);
  REQUIRE(first_session != second_session);

  football_render_snapshot snapshot = {0};
  football_render_snapshot original = snapshot;
  REQUIRE(football_get_render_snapshot(first_session, 0, NULL) == 0);
  REQUIRE(football_get_render_snapshot(first_session, UINT64_MAX, &snapshot) == 0);
  REQUIRE(memcmp(&snapshot, &original, sizeof(snapshot)) == 0);

  REQUIRE(football_get_render_snapshot(first_session, 0, &snapshot) == 1);
  REQUIRE(snapshot.tick == 0);
  REQUIRE(snapshot.px == 0.0f);
  REQUIRE(snapshot.py == 0.0f);
  REQUIRE(snapshot.pz == first_ball.radius);
  REQUIRE(snapshot.qx == 0.0f);
  REQUIRE(snapshot.qy == 0.0f);
  REQUIRE(snapshot.qz == 0.0f);
  REQUIRE(snapshot.qw == 1.0f);

  snapshot.px = 123.0f;
  REQUIRE(football_get_render_snapshot(first_session, 0, &snapshot) == 1);
  REQUIRE(snapshot.px == 0.0f);
  REQUIRE(football_get_render_snapshot(second_session, 0, &snapshot) == 1);
  REQUIRE(snapshot.pz == second_ball.radius);

  football_session_start(first_session);
  football_session_start(first_session);

  football_session_destroy(second_session);
  football_session_destroy(first_session);
  return 1;
}

static int TestSessionCopiesCallerOwnedModels(void) {
  football_pitch_model pitch = DefaultPitch();
  football_ball_model ball = DefaultBall();
  const float original_radius = ball.radius;
  football_session* session = football_session_create(&pitch, &ball, NULL);
  REQUIRE(session != NULL);
  memset(&pitch, 0, sizeof(pitch));
  memset(&ball, 0, sizeof(ball));

  football_render_snapshot snapshot;
  REQUIRE(football_get_render_snapshot(session, 0, &snapshot) == 1);
  REQUIRE(snapshot.pz == original_radius);
  football_session_destroy(session);

  football_session* zero_model_session = football_session_create(&pitch, &ball, NULL);
  REQUIRE(zero_model_session != NULL);
  football_session_destroy(zero_model_session);
  return 1;
}

static int TestReplaySessionReadsHistoryFile(void) {
  const char* path = "football_capi_replay.jsonl";
  FILE* file = fopen(path, "wb");
  REQUIRE(file != NULL);
  fputs("{\"type\":\"header\",\"version\":1,\"ticks_per_second\":100}\n", file);
  fputs("{\"type\":\"snapshot\",\"tick\":0,\"position\":[1.5,2.5,0.75],"
        "\"velocity\":[0,0,0],\"angular_velocity\":[0,0,0],"
        "\"orientation\":[0,0,0,1]}\n",
        file);
  fputs("{\"type\":\"snapshot\",\"tick\":1,\"position\":[2.5,3.5,1.75],"
        "\"velocity\":[0.5,0,0],\"angular_velocity\":[0,0,1],"
        "\"orientation\":[0.5,0,0,0.5]}\n",
        file);
  fclose(file);

  football_pitch_model pitch = DefaultPitch();
  football_ball_model ball = DefaultBall();
  football_session* session = football_session_create(&pitch, &ball, path);
  REQUIRE(session != NULL);
  football_session_start(session);

  football_render_snapshot snapshot = {0};
  REQUIRE(football_get_render_snapshot(session, 0, &snapshot) == 1);
  REQUIRE(snapshot.tick == 0);
  REQUIRE(snapshot.px == 1.5f);
  REQUIRE(snapshot.py == 2.5f);
  REQUIRE(snapshot.pz == 0.75f);
  REQUIRE(snapshot.qw == 1.0f);
  REQUIRE(football_get_render_snapshot(session, 1, &snapshot) == 1);
  REQUIRE(snapshot.tick == 1);
  REQUIRE(snapshot.px == 2.5f);
  REQUIRE(snapshot.qx == 0.5f);
  REQUIRE(football_get_render_snapshot(session, 2, &snapshot) == 0);

  football_session_destroy(session);
  remove(path);
  return 1;
}

static int TestReplaySessionRejectsUnusableFiles(void) {
  football_pitch_model pitch = DefaultPitch();
  football_ball_model ball = DefaultBall();
  REQUIRE(football_session_create(&pitch, &ball, "football_capi_absent.jsonl") == NULL);

  const char* path = "football_capi_broken.jsonl";
  FILE* file = fopen(path, "wb");
  REQUIRE(file != NULL);
  fputs("{\"type\":\"header\",\"version\":9,\"ticks_per_second\":100}\n", file);
  fclose(file);
  REQUIRE(football_session_create(&pitch, &ball, path) == NULL);
  remove(path);
  return 1;
}

static int TestEmptyHistoryPathRecords(void) {
  football_pitch_model pitch = DefaultPitch();
  football_ball_model ball = DefaultBall();
  football_session* session = football_session_create(&pitch, &ball, "");
  REQUIRE(session != NULL);
  football_render_snapshot snapshot = {0};
  REQUIRE(football_get_render_snapshot(session, 0, &snapshot) == 1);
  REQUIRE(snapshot.pz == ball.radius);
  football_session_destroy(session);
  return 1;
}

int main(void) {
  if (!TestNullArguments()) return 1;
  if (!TestSessionsHaveIndependentSnapshotCopies()) return 1;
  if (!TestSessionCopiesCallerOwnedModels()) return 1;
  if (!TestReplaySessionReadsHistoryFile()) return 1;
  if (!TestReplaySessionRejectsUnusableFiles()) return 1;
  if (!TestEmptyHistoryPathRecords()) return 1;
  return 0;
}
