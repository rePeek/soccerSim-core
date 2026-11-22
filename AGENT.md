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
and `history_unit` link only their modules and declared dependencies, never
`simcore`. The C11
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
- Prefer semantic names and explicit construction policies (`Recording::Disabled`
  rather than an unexplained `false`). Use designated model initializers when
  multiple adjacent scalar fields would otherwise be ambiguous.
- A public header includes only what it uses.
- `src/` is never a PUBLIC include directory.
- Do not add a dependency from a leaf module back to `env`/`simcore`.
- Keep `Pitch` free of match/rules/actor concepts; it owns geometry and the
  static physics scene only.
- `Ball` is the only authority for ball state. `football::Env` is one match: the
  ball, the pitch and the tick cursor, advanced only by `LoopMain()` (its
  one-tick `Step()` is private). It holds no thread, lock, flag or notion of
  playing, so the same object works for a live host, a batch run and a future
  branch search alike.
- `football::history::History` records what a match looked like: the caller hands
  it the authoritative `BallState` and the committed tick, so it never pulls state
  and never depends on `Ball`, `Env` or the runner. Recording is fixed at
  construction (`Recording`): a disabled recorder drops frames and allocates nothing.
  Frames are 64 bytes and live in a private Chunk's vector. Env records from tick
  zero without a fixed-duration capacity reservation; the vector grows as needed.
  Recording has no retention limit, so callers must choose tick limits that fit
  their memory budget. There is no capacity-reservation parameter. Keep frames
  fixed-size: variable-length data belongs in separate structures. Never include
  `env.hpp` from history.
  `RecordSnapshot()` requires consecutive ticks starting at zero and rejects
  gaps, duplicates and out-of-order ticks with `std::invalid_argument` before
  appending. `ReadSnapshot()` uses that Tick/index invariant and returns a copied
  `std::optional<Snapshot>` under the same mutex as recording.
- The executor owns its match and nothing else. The executor
  (`football::runner::SingleRunner`) does not advance until `Start()`, which
  launches `Env::LoopMain(tick_limit)` on a worker. Start is one-shot; Wait and
  the destructor join without stopping it. There is no external Stop API.
- `runner.history().ReadSnapshot(tick)` copies frames safely during execution.
  Chunk stays private. `EnvironmentAfterJoin()` returns `const Env&` before
  Start or after Wait, and throws `std::logic_error` while the thread is joinable.
  The host serialises lifecycle calls and keeps the runner alive during reads.
- There are no match phases, fixed match durations or Finished state.
  `Step()` is private. `LoopMain(tick_limit)` advances that many ticks unless
  execution fails; zero means no advancement, not an unbounded run. Each call
  counts additional ticks from the current cursor.
- Never read or mutate live `Env` state concurrently with its executor; the
  cursor is plain state. The sole concurrent read exception is the
  synchronised `History::ReadSnapshot()` path, which does not read live state.
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
- C ABI render reads copy Tick, position and orientation into a 40-byte
  `football_render_snapshot`; never expose internal frame pointers or add a
  duplicate render history. Reads return 1 on success, 0 for invalid parameters
  or unavailable ticks, and leave output unchanged on failure. A session must
  stay alive throughout each call; destruction must not race with reads.
- Do not reintroduce animation, player, team, referee, event, observation or AI
  code without a deliberate architecture decision.
