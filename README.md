# soccerSim-core

A minimal deterministic C++23 football **ball-physics core**:

- `Pitch` owns the pitch description and the static physics scene.
- `Ball` owns the authoritative ball state, integration and contact response.
- `football::Env` is one match: the ball, the pitch, the tick cursor and the
  derived match phase, advanced only by `Step()`.
- `football::runner::SingleRunner` owns one match and the worker that plays it.
- `football::history` records 64-byte state frames of a match in-process.
- The C ABI is one handle over one runner.

There is no match clock, team, player, referee, event, observation, AI or
Runtime layer in this build.

## Build

```sh
nix develop --command bash -c '
  cmake --preset debug
  cmake --build --preset debug -j $(nproc)
  ctest --preset debug
'
```

Presets are `debug` and `release`; the release build runs the same steps with
`--preset release`.

## C API

`include/football/capi.h` is a pure C interface exported by `simcore`, and
`models.h` defines the model structs plus their C++ aliases in one header for
both languages. Callers build plain model values themselves; the handle copies
them, so the caller may modify or discard them afterwards.

```c
#include "football/capi.h"

int main(void) {
  football_pitch_model pitch = {105.0f, 68.0f, 0.04f, 1.6f, 0.015f};
  football_ball_model ball = {0.43f, 0.11f, 0.02f};

  football_session* session = football_session_create(&pitch, &ball);
  if (session == NULL) return 1;

  football_session_destroy(session);  /* stops the loop and flushes the log */
  return 0;
}
```

A handle owns one match and its loop thread: `football_session_create` starts
the match on a worker and returns, while `football_session_destroy` stops and
joins that loop, releases the match and writes the buffered log. A match is
always 90 minutes at 100 Hz (540000 ticks) with half time at 45, and a NULL model
or a loop thread that cannot start makes creation return NULL. The ABI exposes
model values and lifecycle only: there is no play/pause, progress query or state
access yet. A session records its frames in-process, but nothing delivers them
through the ABI yet.

To build the example on Linux, from the repository root:

```sh
cc -std=c11 -Iinclude example.c -Lbuild/debug -lsimcore \
  -Wl,-rpath,"$PWD/build/debug" -o example
```

## Layout

```text
include/football/
├── foundation/   math/, time/, geometry/, collision/
├── pitch/        Pitch description and static scene
├── ball/         Ball and BallState
├── history/      Snapshot, Chunk and History: frame recording
├── models.h      shared C model definitions, defaults, C++ aliases
├── env.hpp       Env: the match state and rules
├── runner.hpp    football::runner: SingleRunner
└── capi.h        pure C ABI

src/
├── foundation/   football::foundation, football::geometry, football::collision
├── pitch/        football::pitch
├── ball/         football::ball
├── history/      football::history
├── env.cpp       Env: the ball, the pitch, the cursor and the phase
├── runner.cpp    SingleRunner: the worker that plays one match
└── capi.cpp      C ABI adapter

unittests/
├── collision/    collision_unit
├── pitch/        pitch_unit
├── ball/         ball_unit
├── history/      history_unit
├── env/          env_unit
├── runner/       runner_unit
└── capi/         capi_unit (compiled as C)
```

## Design rules

- One physical coordinate system: x is pitch length, y is pitch width, z is up;
  the centre spot is the origin.
- SI units and a native 100 Hz (10 ms) tick; physics is deterministic and
  RNG-free.
- `Ball` is the only authority for ball state.
- `Pitch` owns geometry and the static scene only; no rules or actors.
- `Env` holds no thread, lock or play state: one executor owns it and only
  `Step()` advances it.
- `football::history` records 64-byte `Snapshot` frames of state handed to it by
  `Env`; a full match is ~33 MiB, reserved up front, never allocated per frame.
  Recording is fixed when the match is built and a disabled recorder allocates
  nothing.
- One match, one worker: `SingleRunner` starts the loop, `Wait()` joins a
  finished run, `Stop()` ends one early, and the match stays readable until the
  runner dies.
- The C ABI exposes model values and lifecycle only; `src/` is never a public
  include directory.

`AGENT.md` records the conventions in more detail.

## License

Apache-2.0. See `LICENSE`.
