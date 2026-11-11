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

## Layout

```text
include/football/
├── foundation/   math/, time/, geometry/, collision/
├── pitch/        Pitch description and static scene
├── ball/         Ball, BallModel, BallState and contact/response
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
