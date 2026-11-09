# soccerSim-core

A minimal deterministic C++23 football **ball-physics core**:

- `Pitch` owns the pitch description and static physics scene.
- `Ball` owns the authoritative ball state, integration and contact response.
- `GameEnv` is a minimal facade over one Ball on one Pitch.

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
└── env.hpp       minimal GameEnv facade

src/
├── foundation/   football::foundation, football::geometry, football::collision
├── pitch/        football::pitch
├── ball/         football::ball
└── env.cpp       minimal GameEnv implementation

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

## Animation manifest

`assets/animations/manifest.json` lists the authored clips. `id` starts at 1 and
is contiguous in catalog order; `file` is the GLB path relative to the
manifest directory. The historical `name` field is not stored.

- `frame_count` — number of 100 Hz pose samples; the clip span is
  `frame_count - 1` native ticks.
- `metadata` — selection/touch facts: velocities, body/ball angles and
  directions, difficulty, quadrant, foot, retain/special state, touch body part
  and touch power/difficulty factors.
- `contact_position` — first touch ball position, or `null` when the clip has
  no touch.
- `touches` — all authored touches as `{frame, position}` entries.

### `metadata.action_type`

`action_type` is the numeric `e_DefString` catalog tag, not `e_FunctionType`:

| value | tag | meaning |
|---:|---|---|
| 6 | `Trap` | trap / receive |
| 7 | `Deflect` | deflect |
| 8 | `Interfere` | interfere / tackle |
| 9 | `Trip` | trip |
| 10 | `ShortPass` | short pass |
| 11 | `LongPass` | long pass |
| 12 | `Shot` | shot |
| 13 | `Sliding` | sliding tackle |
| 14 | `Movement` | locomotion / movement |
| 15 | `Special` | special action |
| 16 | `BallControl` | ball control |
| 17 | `HighPass` | high pass |
| 18 | `Catch` | goalkeeper catch |

All manifest vectors/angles use the Stadium Frame (x length, y width, z up),
metres and radians, with 100 Hz frame numbers.

## License

Apache-2.0. See `LICENSE`.
