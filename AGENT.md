# AGENT.md

Guidance for AI agents working in this repository. This file describes the
rules that are true *now*.

## Project

`soccerSim-core` is now a minimal deterministic C++23 ball-physics core:

- `foundation` — math, time, geometry and generic collision primitives
- `pitch` — pitch description and static physics scene
- `ball` — authoritative ball model, physics and contact response
- `env.cpp` / `include/football/env.hpp` — minimal `Env` facade over one
  Ball on one Pitch

There is no match clock, team, player, referee, event, observation, AI or
Runtime layer in this build.

## Build and test

CMake >= 3.24, Ninja, C++23; use the `flake.nix` dev shell.

```sh
# Debug build + pitch/ball suites
nix develop --command bash -c 'cmake --preset debug && cmake --build --preset debug -j $(nproc) && ctest --preset debug'

# Release
nix develop --command bash -c 'cmake --preset release && cmake --build --preset release -j $(nproc) && ctest --preset release'
```

The only test suites are `pitch_unit` and `ball_unit`. They link only their
module and its declared dependencies, never `simcore`.

## Coordinate and physics conventions

- One physical coordinate system: x is pitch length, y is pitch width, z is up;
  the centre spot is the origin.
- SI units: metres, seconds, kilograms, radians. The native tick is 100 Hz
  (10 ms).
- Physics is deterministic and RNG-free. Behaviour must be derived from
  explainable state (skill, contact geometry, body orientation, ball velocity).
- Keep floating-point expressions explicit and ordered; do not reorder
  arithmetic in a way that changes results without an intentional, tested
  semantic change.
- Invalid inputs are rejected at the owning module boundary, not silently
  clamped downstream.
- Ground physics keeps the empirical friction values in `Pitch`.

## Source layout

```text
include/football/
├── foundation/   math/, time/, geometry/, collision/
├── pitch/        Pitch description and static scene
├── ball/         Ball, BallModel, BallState, impulse/contact/response
└── env.hpp       minimal Env facade

src/
├── foundation/   football::foundation, football::geometry, football::collision
├── pitch/        football::pitch
├── ball/         football::ball
└── env.cpp       minimal Env implementation

unittests/
├── pitch/        pitch_unit
└── ball/         ball_unit
```

## Coding conventions

- Prefer explicit values and narrow interfaces over comments that restate code.
- A public header includes only what it uses.
- `src/` is never a PUBLIC include directory.
- Do not add a dependency from a leaf module back to `env`/`simcore`.
- Keep `Pitch` free of match/rules/actor concepts; it owns geometry and the
  static physics scene only.
- `Ball` is the only authority for ball state; `Env` only owns the
  composition and lifecycle.
- Do not reintroduce animation, player, team, referee, event, observation or AI
  code without a deliberate architecture decision.
