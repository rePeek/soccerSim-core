# soccerSim-core

A minimal deterministic C++23 football **ball-physics core**:

- `Pitch` owns the pitch description and static physics scene.
- `Ball` owns the authoritative ball state, integration and contact response.
- `Env` is a minimal facade over one Ball on one Pitch.

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

## C API

`include/football/capi.h` is a pure C interface exported by `simcore`.
`models.h` defines the model structs and the C++ aliases in one header for
both C and C++. Callers construct plain model values themselves; the C API owns
one `Env` internally: `football_init` copies the models and constructs it,
`football_shutdown` destroys it. There is no error channel or handle.

```c
#include "football/capi.h"

int main(void) {
  football_pitch_model pitch = {105.0f, 68.0f, 0.04f, 1.6f, 0.015f};
  football_ball_model ball = {0.43f, 0.11f, 0.02f};
  pitch.length = 100.0f;
  football_init(&pitch, &ball);
  football_shutdown();
  return 0;
}

For example, on Linux, from the repository root:

```sh
cc -std=c11 -Iinclude example.c -Lbuild/debug -lsimcore \
  -Wl,-rpath,"$PWD/build/debug" -o example
```

This initial API exposes configuration and lifecycle only, not simulation
stepping or state access. See [C/C# ABI usage](docs/capi.md) for the contract
and P/Invoke declarations.

## Layout

```text
include/football/
├── foundation/   math/, time/, geometry/, collision/
├── pitch/        Pitch description and static scene
├── ball/         Ball, BallModel, BallState and contact/response
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

## Design rules

- One Stadium Frame: x is pitch length, y is pitch width, z is up; centre spot
  is the origin.
- SI units and a native 100 Hz (10 ms) tick.
- Physics is deterministic and RNG-free.
- `Ball` is the only authority for ball state.
- `Pitch` owns geometry and the static scene only; no rules or actors.
- `src/` is never a public include directory.

## License

Apache-2.0. See `LICENSE`.
