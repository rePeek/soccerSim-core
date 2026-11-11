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
- `capi.h` / `capi.cpp` — C ABI adapter for C and C#; see `docs/capi.md`
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
`simcore` to exercise the exported C ABI.

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
- Model values are caller-owned, unchecked configuration. Do not add implicit
  range/finiteness validation or clamping for `PitchModel` / `BallModel`.
- Ground physics keeps the empirical friction values in `Pitch`.

## Source layout

```text
include/football/
├── foundation/   math/, time/, geometry/, collision/
├── pitch/        Pitch description and static scene
├── ball/         Ball, BallModel, BallState, impulse/contact/response
├── models.h      shared C model definitions, defaults, C++ aliases
├── env.hpp       minimal Env facade
└── capi.h        pure C ABI

src/
├── foundation/   football::foundation, football::geometry, football::collision
├── pitch/        football::pitch
├── ball/         football::ball
├── env.cpp       minimal Env implementation
└── capi.cpp      C ABI adapter

unittests/
├── pitch/        pitch_unit
├── ball/         ball_unit
└── capi/         capi_unit (compiled as C)
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
- `PitchModel` / `BallModel` alias the C structs defined once in `models.h`;
  never duplicate model layouts. Initialize every field explicitly: `{}`
  produces zeros in C++ too, not physical defaults.
- Models are copied without validation by `Pitch` / `Ball`. C ABI NULL checks
  and exception translation remain.
- C ABI headers must compile as C; keep C++ types and exceptions behind the
  adapter. Model structs are caller-owned values; the adapter owns a single Env.
- Do not reintroduce animation, player, team, referee, event, observation or AI
  code without a deliberate architecture decision.
