# AGENT.md

Guidance for AI agents working in this repository. This file describes the
rules that are true *now*.

## Project

`soccerSim-core` is now a minimal deterministic C++23 ball-physics core:

- `foundation` — math, time, geometry and generic collision primitives
- `pitch` — pitch description and static physics scene
- `ball` — authoritative ball model, physics and contact response
- `history` — `football::history`: records fixed-size `Snapshot` frames of match
  state into a `Chunk`; callers hand it state values, so it never depends on
  `Ball`, `Env` or the runner
- `env.cpp` / `include/football/env.hpp` — `football::Env`: Ball, Pitch, History
  and the tick cursor, advanced through `LoopMain(tick_limit)`
- `src/runner.cpp` / `include/football/runner.hpp` — `football::runner`: owns
  one environment and starts its bounded loop on a worker explicitly
- `capi.h` / `capi.cpp` — C ABI adapter for C and C#; one handle per match, model
  values, start, lifecycle and copied render snapshots
- `models.h` — single C header: model layouts/defaults plus C++ aliases

There is no match clock, team, player, referee, event, observation, AI or
Runtime layer in this build.


## Build and test

CMake >= 3.24, Ninja, C11 and C++23; use the `flake.nix` dev shell.

```sh
nix develop --command bash -c 'cmake --preset debug && cmake --build --preset debug -j $(nproc) && ctest --preset debug'

nix develop --command bash -c 'cmake --preset release && cmake --build --preset release -j $(nproc) && ctest --preset release'
```

The domain suites `foundation_unit`, `collision_unit`, `pitch_unit`, `ball_unit`
`history_unit` and `trajectory_unit` link only their modules and declared
dependencies, never `simcore`. `trajectory_unit` combines `football::ball`,
`football::history` and nlohmann_json to simulate 1000-tick trajectories and
verify the saved JSON Lines. The C11
`capi_unit` consumer links `simcore` to exercise the exported C ABI, and
`env_unit` plus `runner_unit` link it because `Env` and `SingleRunner` are
compiled into the shared library rather than into modules of their own.

## Coordinate and physics conventions

- One physical coordinate system: x is pitch length, y is pitch width, z is up;
  the centre spot is the origin.
- SI units: metres, seconds, kilograms, radians. The native tick is 100 Hz
  (10 ms).
- Physics is deterministic and RNG-free. Behaviour must be derived from
  explainable state (contact geometry, ball orientation, ball velocity).
- Each tick predicts collision-free motion first, then sweeps that path. With
  no impact it commits the prediction. At the first impact it places the ball at
  contact and applies a passive impulse to the full-tick predicted velocity.
  Orientation also retains the full-tick prediction. There is no TOI-based time
  split and no post-impact tail motion.
- A rolling ball is anchored to the grass by its integrator, so the ground is
  not a candidate contact: the sweep skips it and queries only the goal
  furniture. A tick that starts tangent to a surface while separating flies free
  and may end at most g/2*tick^2 (~0.5 mm) below the grass; the next tick catches
  the ball. Anomalous starts are fixed once, not by a repair loop.
- Keep floating-point expressions explicit and ordered; do not reorder
  arithmetic in a way that changes results without an intentional, tested
  semantic change.
- Model values are caller-owned, unchecked configuration. Do not add implicit
  range/finiteness validation or clamping for `PitchModel` / `BallModel`.
- Ground physics keeps the empirical friction values in `Pitch`.
- Keep conventional mathematical names such as `dt` for integration intervals
  in seconds; do not expand standard notation merely to make identifiers longer.
  Collision `impact_fraction` is dimensionless, in [0, 1], not a duration.
- The legacy Euler operations intentionally have distinct names:
  `SetEulerAnglesYZX()` constructs qY * qZ * qX, while `GetEulerAnglesXYZ()`
  extracts XYZ angles. Do not assume they are inverse operations or change their
  numerical formulas as part of readability cleanup. General non-unit quaternion
  inversion and Euler singularities remain separate mathematical audit work.

## Source layout

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
├── runner.cpp    football::runner: SingleRunner
├── env.cpp       Env: the ball, the pitch and the cursor
└── capi.cpp      C ABI adapter

unittests/
├── foundation/   foundation_unit
├── collision/    collision_unit
├── pitch/        pitch_unit
├── ball/         ball_unit
├── history/      history_unit
├── trajectory/   trajectory_unit (ball + history + JSON Lines)
├── env/          env_unit (links simcore)
├── runner/       runner_unit (links simcore)
└── capi/         capi_unit (compiled as C)
```

## Coding conventions

- Do not add comments to repository-owned source code, headers, tests, build
  scripts, configuration files or documentation code examples. This includes
  line comments, block comments, documentation comments, namespace-closing
  comments, parameter-label comments and commented-out code. Use clear names
  and structure; put necessary explanations in Markdown prose instead.
  Preserve third-party code and its required license/copyright notices.
- Prefer semantic names and explicit construction policies (`HistoryMode::Replay`
  rather than an unexplained `false`). Use designated model initializers when
  multiple adjacent scalar fields would otherwise be ambiguous.
- A public header includes only what it uses.
- `src/` is never a PUBLIC include directory.
- Do not add a dependency from a leaf module back to `env`/`simcore`.
- Keep `Pitch` free of match/rules/actor concepts; it owns geometry and the
  static physics scene only.
- `Ball` is the only authority for ball state. `football::Env` is one match: the
  ball, the pitch, the tick cursor and the `History` it builds itself, advanced only by
  `LoopMain()` (its one-tick `Step()` is private). It owns the mutex that guards
  its cursor, history and reads, but holds no thread, flag or notion of playing,
  so the same object works for a live host, a batch run and a future branch
  search alike. `Env` builds its History from an optional JSON Lines path: an empty
  path records a new match, a path loads that file in `Replay` mode, which records
  nothing and makes `LoopMain()` return without advancing, so a loaded trajectory
  can be hosted without simulating. `History` never appears in the C ABI.
- `football::history::History` records what a match looked like: the caller hands
  it the authoritative `BallState` and the committed tick, so it never pulls state
  and never depends on `Ball`, `Env` or the runner. Its mode is fixed at
  construction: `History()` records, and `Replay` is created only by `LoadJsonl()`
  and rejects recording. `mode()` reports which one applies. Every history owns a
  Chunk in both modes, so a loaded trajectory is a first-class value, not a
  special case.
  Frames are 64 bytes and live in a private Chunk's vector. Env records from tick
  zero without a fixed-duration capacity reservation; the vector grows as needed.
  Recording has no retention limit, so callers must choose tick limits that fit
  their memory budget. There is no capacity-reservation parameter. Keep frames
  fixed-size: variable-length data belongs in separate structures. Never include
  `env.hpp` from history.
  `RecordSnapshot()` requires consecutive ticks starting at zero and rejects
  gaps, duplicates and out-of-order ticks with `std::invalid_argument` before
  appending; in `Replay` mode it throws `std::logic_error` instead of mutating a
  loaded trajectory. `ReadSnapshot()` uses that Tick/index invariant and returns a
  copied `std::optional<Snapshot>`; it holds no lock, because `Env` guards it.
- `History` is a movable value type. Only `Env` constructs it, from the history path
  it is given; the C ABI passes a path string and never names the type.
  `History::SaveJsonl(path)` writes JSON Lines: one header object holding the
  format version and tick rate, then one object per tick with the full state
  (position, velocity, angular velocity, orientation in x/y/z/w order). Floats
  round-trip exactly when read back as float. It writes a sibling `.tmp` file and
  renames it into place, so a failed write leaves the previous file intact and
  throws `std::runtime_error`.
- `History::LoadJsonl(path)` is the read-only entry: it validates the header
  (type, format version, tick rate) and every frame (type, consecutive ticks from
  zero, array lengths of 3/3/3/4), then builds the Chunk and returns a History in
  `Replay` mode: readable and saveable, but `RecordSnapshot()` throws
  `std::logic_error`. It throws `std::runtime_error` with file, line and
  reason on a missing file, empty file, malformed JSON or inconsistent data, and
  leaves no partially loaded state. `Env` and `SingleRunner` always construct a
  recording history, so a simulated match can never write into a loaded
  trajectory. Callers choose and pass the path; History never decides where
  files live. JSON Lines is the current interchange format
  because it is inspectable; a binary format may be added later without changing
  Snapshot or Chunk. nlohmann_json is a PRIVATE dependency of football::history
  and never appears in its public headers.
- The executor owns its match and nothing else. The executor
  (`football::runner::SingleRunner`) does not advance until `Start()`, which
  launches `Env::LoopMain(tick_limit)` on a worker. Start is one-shot; Wait and
  the destructor join without stopping it. There is no external Stop API.
- `SingleRunner::Environment()` returns `const Env&` at any time; `Env` owns the
  mutex that guards its cursor, its history and every snapshot read, so
  `ReadSnapshot(tick)`, `Ticks()`, `frame_count()` and `history_mode()` are safe
  while the worker records. Mutation stays private to `Step()`.
- There are no match phases, fixed match durations or Finished state.
  `Step()` is private. `LoopMain(tick_limit)` advances that many ticks unless
  execution fails; zero means no advancement, not an unbounded run. Each call
  counts additional ticks from the current cursor.
`History` itself has no lock: it is a plain value that `Env` owns and guards.
  Never reach a History directly while its Env is running; go through
  `Env::ReadSnapshot()`, which locks. `Env` never exposes mutable state while the
  worker runs, and its `Step()` is private.
  Exceptions must not cross the thread boundary: the match logs them and the
  loop stops.
- One match, one worker: only the loop advances `Env`, and there is no external
  stop. `Wait()` and the destructor join it, so a live session is always played
  out before its `Env` dies. The runner is declared after the `Env` it owns.
- `PitchModel` / `BallModel` alias the C structs defined once in `models.h`;
  never duplicate model layouts. Initialize every field explicitly: `{}`
  produces zeros in C++ too, not physical defaults.
- Models are copied without validation by `Pitch` / `Ball`. C ABI NULL checks
  and exception translation remain.
- C ABI headers must compile as C; keep C++ types and exceptions behind the
  adapter. Model structs are caller-owned values; the adapter owns one match per
  `football_session` handle — a `SingleRunner` owning one match —
  never a singleton.
- Every match is independent: no static or thread-local simulation state and no
  RNG, so any number may run at once and identical inputs give identical results.
  The only process-wide piece is the log, which is thread-safe; destroying a
  session flushes it, so no separate flush call exists.
`football_session_create` takes a models pair, a tick limit and an optional
  JSON Lines path: a NULL or empty path records a new match, otherwise the file is
  loaded and the session only replays it, without simulating.
- C ABI render reads copy Tick, position and orientation into a 40-byte
  `football_render_snapshot`; never expose internal frame pointers or add a
  duplicate render history. Reads return 1 on success, 0 for invalid parameters
  or unavailable ticks, and leave output unchanged on failure. A session must
  stay alive throughout each call; destruction must not race with reads.
- Do not reintroduce animation, player, team, referee, event, observation or AI
  code without a deliberate architecture decision.
