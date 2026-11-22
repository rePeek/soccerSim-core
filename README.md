# soccerSim-core

A minimal deterministic C++23 football **ball-physics core**:

- `Pitch` owns the pitch description and the static physics scene.
- `Ball` owns the authoritative ball state, integration and contact response.
- `football::Env` owns the ball, pitch, history and tick cursor, advanced only
  by `LoopMain(tick_limit)`. There is no match phase or fixed match duration.
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
  football_pitch_model pitch = {
      .length = 105.0f, .width = 68.0f, .quadratic_resistance = 0.04f,
      .ground_deceleration = 1.6f, .air_drag = 0.015f};
  football_ball_model ball = {
      .mass = 0.43f, .radius = 0.11f, .magnus_coefficient = 0.02f};

  football_session* session = football_session_create(&pitch, &ball, 100, NULL);
  if (session == NULL) return 1;

  football_session_start(session);

  football_render_snapshot snapshot;
  if (!football_get_render_snapshot(session, 0, &snapshot)) {
    football_session_destroy(session);
    return 2;
  }

  football_session_destroy(session);
  return 0;
}
```

`football_session_create` prepares the environment, copies the models and builds
the history. `football_session_start` launches the loop once, and
`football_session_destroy` waits for completion before releasing the environment
and flushing the log. The third argument specifies how many ticks to advance:
zero advances no ticks and keeps the initial snapshot, positive limits run that
many ticks unless execution fails. The fourth argument names a JSON Lines history
to replay: when it is NULL or empty the session records a new match, otherwise it
loads that file and never simulates, so ticks stay at zero and the loaded frames
are only read. There is no fixed match duration or external stop. History grows
as needed; callers should choose tick limits that fit their memory budget. A NULL
model makes creation return NULL.

`football_get_render_snapshot(session, tick, output)` copies one recorded pose
while the worker runs. It returns 1 on success, or 0 for a NULL argument or an
unavailable tick, leaving output unchanged on failure. Tick zero is recorded
before creation returns; a future tick can be retried later. The 40-byte result
contains the tick, position in metres and orientation quaternion in x/y/z/w order,
not velocity or angular velocity. The session must stay alive during each call;
destruction must not race with reads. There is no play/pause or progress API.

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
├── env.cpp       Env: the ball, the pitch, the history and the cursor
├── runner.cpp    SingleRunner: the worker that plays one match
└── capi.cpp      C ABI adapter

unittests/
├── foundation/   foundation_unit
├── collision/    collision_unit
├── pitch/        pitch_unit
├── ball/         ball_unit
├── history/      history_unit
├── trajectory/   trajectory_unit (1000-tick ball trajectories saved as JSON Lines)
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
  `LoopMain()` advances it; its one-tick `Step()` is private.
- `football::history` records 64-byte `Snapshot` frames of state handed to it by
  `Env`. History retains every tick in a growing vector; there is no fixed-duration
  reservation or retention limit.
  Construction is fixed: `History()` records, while `HistoryMode::Replay` comes only
  from `LoadJsonl()` and rejects recording with `std::logic_error`. `mode()` reports
  the current mode, and both modes own a Chunk.
  `RecordSnapshot()` requires consecutive ticks starting at zero and rejects gaps
  or duplicates with `std::invalid_argument`; `ReadSnapshot()` returns a copied
  `std::optional<Snapshot>` indexed by Tick.
- `History::SaveJsonl(path)` writes one header line (format version and tick rate)
  then one JSON object per tick holding the complete state. Writes go to a
  sibling `.tmp` file that is renamed into place, so a failed save keeps the old
  file and throws `std::runtime_error`.
- `History::LoadJsonl(path)` reads such a file back: it validates the header and
  every frame, rejects malformed or non-consecutive data with `std::runtime_error`
  naming the file and line, and returns a History in `Replay` mode: readable and
  saveable, but not recordable. `Env` and `SingleRunner` always record, so a
  simulated match can never write into a loaded trajectory. Both functions take the path from the caller; History never chooses
  where files live.
- `SingleRunner` starts only on `Start()`. `Wait()` and the destructor join the
  worker, which executes `Env::LoopMain(tick_limit)`. There is no external Stop.
  Each LoopMain call advances additional ticks; a zero limit is a no-op.
- The C ABI exposes model values, explicit start and copied render snapshots;
  `src/` is never a public include directory.
- `Chunk` remains private storage inside History, with no raw accessors.
  `runner.history().ReadSnapshot(tick)` copies recorded frames safely while the
  worker runs. `runner.EnvironmentAfterJoin()` returns only `const Env&` before
  `Start()` or after `Wait()` joins the worker, and throws `std::logic_error`
  while the worker runs. Lifecycle operations, environment access and destruction
  must be serialised by the host; concurrent history reads require the runner to
  remain alive.

## Math conventions

`dt` denotes the integration interval in seconds. Collision `impact_fraction`
is a dimensionless fraction of the swept interval, not a duration.
`Vector3::Extrapolate()` accepts velocity in metres per second and a duration
explicitly named `duration_milliseconds`.

The legacy Euler operations use different conventions: `SetEulerAnglesYZX()`
constructs qY * qZ * qX; `GetEulerAnglesXYZ()` extracts XYZ angles. They are not
a round-trip pair. `FromRotatedNegativeZAxis()` explicitly computes the direction
that used to be hidden in quaternion-to-vector assignment. This readability
refactor preserves the existing numerical algorithms; general non-unit quaternion
inversion and Euler singularities require a separate mathematical audit.

`AGENT.md` records the conventions in more detail.

## License

Apache-2.0. See `LICENSE`.
