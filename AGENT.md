# AGENT.md

Guidance for AI agents working in this repository. This file describes the
rules that are true *now*.

## Project

`soccerSim-core` is now a minimal deterministic C++23 ball-physics core:

- `foundation` — math, time, geometry and generic collision primitives
- `pitch` — pitch description and static physics scene
- `ball` — authoritative ball model, physics and contact response
- `env.cpp` / `include/football/env.hpp` — `Env`: one match's state and rules —
  one Ball on one Pitch, the tick cursor and the derived match boundaries; it
  advances only when someone calls `Step()`
- `src/runner.cpp` / `include/football/runner.hpp` — `football::runner`: the
  execution side. `SingleRunner` owns one match and one worker, and holds
  play/pause
- `capi.h` / `capi.cpp` — C ABI adapter for C and C#; one handle per match, model
  values, lifecycle and the control/progress surface
- `models.h` — single C header: model layouts/defaults plus C++ aliases

There is no match clock, team, player, referee, event, observation, AI or
Runtime layer in this build.


## Build and test

CMake >= 3.24, Ninja, C11 and C++23; use the `flake.nix` dev shell.

```sh
# Debug build + pitch/ball suites
nix develop --command bash -c 'cmake --preset debug && cmake --build --preset debug -j $(nproc) && ctest --preset debug'

# Release
nix develop --command bash -c 'cmake --preset release && cmake --build --preset release -j $(nproc) && ctest --preset release'
```

The domain suites `pitch_unit` and `ball_unit` link only their modules and
declared dependencies, never `simcore`. The C11 `capi_unit` consumer links
`simcore` to exercise the exported C ABI, and `env_unit` links it because `Env`
is compiled into the shared library rather than into a module of its own.

## Coordinate and physics conventions

- One physical coordinate system: x is pitch length, y is pitch width, z is up;
  the centre spot is the origin.
- SI units: metres, seconds, kilograms, radians. The native tick is 100 Hz
  (10 ms).
- Physics is deterministic and RNG-free. Behaviour must be derived from
  explainable state (skill, contact geometry, body orientation, ball velocity).
- Each tick predicts collision-free motion first, then sweeps that path. With
  no impact it commits the prediction. At the first impact it places the ball at
  contact and applies a passive impulse to the state integrated up to that
  instant. The rest of the tick is not integrated, so there is no TOI-based time
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

## Source layout

```text
include/football/
├── foundation/   math/, time/, geometry/, collision/
├── pitch/        Pitch description and static scene
├── ball/         Ball, BallState and BallSnapshot (the renderable frame)
├── models.h      shared C model definitions, defaults, C++ aliases
├── env.hpp       Env: the match state and rules
├── runner.hpp    football::runner: SingleRunner
└── capi.h        pure C ABI

src/
├── foundation/   football::foundation, football::geometry, football::collision
├── pitch/        football::pitch
├── ball/         football::ball
├── runner.cpp    football::runner: SingleRunner
├── env.cpp       Env: the ball, the pitch and the cursor
└── capi.cpp      C ABI adapter

unittests/
├── pitch/        pitch_unit
├── ball/         ball_unit
├── env/          env_unit (links simcore)
├── runner/       runner_unit (links simcore)
└── capi/         capi_unit (compiled as C)
```

## Coding conventions

- Prefer explicit values and narrow interfaces over comments that restate code.
- A public header includes only what it uses.
- `src/` is never a PUBLIC include directory.
- Do not add a dependency from a leaf module back to `env`/`simcore`.
- Keep `Pitch` free of match/rules/actor concepts; it owns geometry and the
  static physics scene only.
- `Ball` is the only authority for ball state. `Env` is one match: the ball, the
  pitch and the tick cursor, advanced only by `Step()`. It holds no
  thread, lock, flag or notion of playing, so the same object works for a live
  host, a batch run and a future branch search alike.
- Playing, pausing and stopping are the executor's state, not the match's. The
  executor (`football::runner::SingleRunner`) owns its `Env` and one worker and
  knows nothing about time or match state: it only decides whether the loop keeps
  calling `Step()`. `IsRunning()` is the whole of what it reports - asked to run and
  the worker is alive - and it says nothing about the tick.
- The match is read through `Environment()`, and only at a quiescent point:
  `Pause()` returns once the worker is parked, and a loop that reached full time
  has ended. Nothing is published or duplicated, so a host that wants live progress
  while the match runs has to wait for the loop to end first. Pacing to real time or
  per-frame tick budgets belong to the host, not to the core.
- Match boundaries are derived from the cursor and then cached: `Env::Step()` is the
  only writer of both the tick and the `MatchPhase` it mirrors, so `Phase()` is a
  plain read that can never disagree with `Ticks()`. Both come from the single
  `PhaseOfTick()` rule, which yields `FirstHalf`, the single tick of `HalfTime` at
  `kHalfTime`, `SecondHalf`, and `FullTime` from `kFullTime` onwards (saturating, so
  it stays full time past the horizon). A host that wants a break to last simply
  holds the match at the half-time tick.
  A match is always 90 minutes at 100 Hz (540000 ticks) with half time at 45
  (270000), both fixed constants rather than configuration. There is no clock,
  break, ceremony, change of ends or added time: half time is only the tick where
  the halves meet, and the last simulated tick is `kFullTime - 1`.
- `Step()` does not police the horizon: it advances exactly one tick and the
  executor stops calling it once `Finished()` reports full time. `Step(duration)`
  is the bounded form and stops at full time on its own.
- Never let two threads touch one `Env`: it is plain state with no synchronisation,
  and time is no exception — the cursor is a plain member counted per match. The
  runner exposes it as `Environment()`, and `Pause()` is the rendezvous that makes
  reading it safe. Exceptions must not cross a thread boundary: the runner logs
  them and ends its loop.
- Teardown is thread lifetime: the loop is a `std::jthread`, so destroying the
  runner requests stop and joins, and a parked loop is woken through the stop
  token rather than a simulation flag. The runner is declared after the `Env` it
  drives so it is destroyed first.
- `PitchModel` / `BallModel` alias the C structs defined once in `models.h`;
  never duplicate model layouts. Initialize every field explicitly: `{}`
  produces zeros in C++ too, not physical defaults.
- Models are copied without validation by `Pitch` / `Ball`. C ABI NULL checks
  and exception translation remain.
- C ABI headers must compile as C; keep C++ types and exceptions behind the
  adapter. Model structs are caller-owned values; the adapter owns one match per
  `football_session` handle — a `SingleRunner` owning one match and one worker —
  never a singleton.
- Every match is independent: no static or thread-local simulation state and no
  RNG, so any number may run at once and identical inputs give identical results.
  The only process-wide piece is the log, which is thread-safe; destroying a
  session flushes it, so no separate flush call exists.
- No frame or observation delivery exists yet. Do not reintroduce chunk or
  snapshot-stream APIs until that boundary is designed: the C ABI today exposes
  lifecycle, `resume`/`pause` and published progress only.
- Do not reintroduce animation, player, team, referee, event, observation or AI
  code without a deliberate architecture decision.
