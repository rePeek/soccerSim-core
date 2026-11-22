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

  football_session* session = football_session_create(&pitch, &ball, NULL);
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
and flushing the log. The third argument names a JSON Lines history to replay:
when it is NULL or empty the session records a new match and advances
`kDefaultRunTicks` (50000 ticks, 500 s) before stopping, otherwise it loads that
file and never simulates, so ticks stay at zero and the loaded frames are only
read. The run length is an internal policy, not a C ABI parameter, so hosts never
pass numbers. History grows as needed and a NULL model makes creation return NULL.

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
- `Env` holds no thread or play state: one executor owns it and only `LoopMain()`
  advances it; its one-tick `Step()` is private. `Env` owns the mutex that guards
  its cursor, its history and the copied snapshot reads.
- `football::history` records 64-byte `Snapshot` frames of state handed to it by
  `Env`, which builds it from an optional JSON Lines path: empty means record,
  a path loads that file for replay. `History` never appears in the C ABI.
  History retains every tick in a growing vector; there is no fixed-duration
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
- `Chunk` remains private storage inside History, with no raw accessors, and
  History carries no lock of its own. `Env` owns the mutex: `Step()` writes under
  it, and `ReadSnapshot()`, `Ticks()`, `frame_count()` and `history_mode()` read
  under it. `SingleRunner::Environment()` returns `const Env&` at any time, so the
  worker and the host may read and record concurrently. Lifecycle calls and
  destruction must still be serialised by the host.

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

## Ground spin dissipation

Grounded motion applies the pitch's empirical grass resistance to both horizontal translation and spin. Spin uses the equivalent surface speed `radius * |angular_velocity|`, reduced by `(ground_deceleration + quadratic_resistance * surface_speed^2) * dt`, with saturation at zero and no reversal. This is a dissipative rolling/spin resistance model, not a no-slip constraint or a coupled sliding-contact solver. It introduces no new model or ABI fields. Airborne spin is unchanged; removing both grass-resistance coefficients preserves grounded spin.

The grounded vertical-speed threshold is at least two ticks of gravitational velocity change. Together with the ground-distance tolerance, this absorbs rebounds whose flight is only a few discrete integration steps, avoiding an endless low-energy bounce cycle that would prevent grass resistance from acting. Larger launches and impacts retain the pitch restitution. All trajectory tests require final linear/angular speed within the rest tolerance and an unchanged final position and orientation.

## Replay trajectories

`trajectory_unit` generates 15-second JSONL replays in the build tree's `unittests/trajectory/trajectories/` directory. `forward.jsonl` and `diagonal.jsonl` use stronger ground kicks with travel exceeding 15 metres. `lofted.jsonl` contains a visible flight arc. `long_pass.jsonl` starts at (-25, -12) metres with initial velocity approximately (20, 6, 12) m/s; it crosses much of the pitch, reaches about 6 metres above ground, bounces and rolls before settling. Its test checks flight time, height, travel distance, pitch bounds, multiple bounces and replay loading. `round_trip.jsonl` remains the exact serialization round-trip example.
