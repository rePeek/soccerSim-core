# AGENT.md

Guidance for AI agents working in this repository.

## What this project is

`soccerSim-core` is a headless **football (soccer) simulation core** written in C++23.
It is a heavily modified fork of
[BazkieBumpercar/GameplayFootball](https://github.com/BazkieBumpercar/GameplayFootball),
which itself descends from Google Research Football and the `blunted2` engine.

The engine is deterministic, animation/physics driven, and exposes a `GameEnv` API
that mirrors the Google Research Football `gfootball` environment (observations as
`SharedInfo`, discrete `action()`, `reset(config)`, and binary `get_state`/`set_state`).

- Remote: `git@github.com:rePeek/soccerSim-core.git`
- License: Apache-2.0 (`LICENSE`); the `blunted` foundation code is public domain
  (the "written by bastiaan konings schuiling 2008 - 2015" boilerplate).

## Branch state

Development happens on **`feat/anim-base`** (the checked-out branch). Treat this
branch and the files on disk as the source of truth.

The `main` branch is **frozen** — it contains an experimental refactor
(`src/foundation/`, `src/core/`, `src/app/`, CPM deps) that is **no longer being
developed**. Do not build against it, port from it, or merge toward it.

`REFACTOR_PLAN.md` is a git-ignored worklog from that abandoned refactor. Its phase
markers reference `main`'s history and do **not** describe the current tree. Read it
only for terminology/intent; it is not the roadmap for this branch.

Do not create `src/core/` or `src/legacy/` directories here (those belong to the
frozen `main` branch).

## Build, run, test

Toolchain: CMake >= 3.24, Ninja, a C++23 compiler. The `flake.nix` dev shell provides
all of them (`nix develop` or `nix-shell`).

```sh
# configure (into build/debug or build/release)
cmake --preset release          # or: debug

# build
cmake --build --preset release

# test
ctest --preset release
```

Runtime note: simulation binaries need the data directory:

```sh
export GFOOTBALL_DATA_DIR="$PWD/data"
```

`build/` is git-ignored. If a preset configure behaves unexpectedly after a large
restructure, reconfigure from scratch with `rm -rf build/<preset>`.

### Test targets

- `football_regression` — the safety net. ~4200 lines in `tools/football_regression.cpp`:
  unit checks (kinematics, body facing, locomotion, colliders, schedulers, command
  adapters), golden-snapshot comparison, save/load round-trips, symmetry
  (`reverse_team_processing`) and A/B animation perturbation checks. Requires
  `GFOOTBALL_DATA_DIR`. Flags: `--print-baseline` (regenerate golden), `--animation-ab`,
  `--animation-ab-lifecycle`.
- `football_smoke` — minimal `GameEnv` headless run (`tools/football_smoke.cpp`);
  optional positional arg is the step count (default 1000).
- `football_headless_core_guard` — shell test (`tools/football_headless_core_guard.sh`)
  asserting `libgame.so` has no graphics `NEEDED` deps (SDL/GL/X11…) and that `src/`
  has no graphics include. **Do not add graphics or Boost dependencies to core.**

### Adding/removing source files

`src/**/*.cpp`/`.hpp` must be registered in **`sources.cmake`** (the single source
list on this branch). A new file that is not listed there will not be compiled.

## Source layout (current branch)

```
src/
├── foundation/     通用基础：数学/几何/日志/工具（原 blunted base）
│   ├── math/           vector3, matrix3/4, quaternion, bluntmath
│   ├── geometry/       aabb, line, plane, triangle, trianglemeshutils
│   ├── log, properties, utils, backtrace, file
│   ├── types/          refcounted, command
│   └── misc/           hungarian（通用算法；perlin 已删）
├── animation/      动画系统
│   ├── animation, xmlloader
│   ├── animcollection, import_hierarchy, import_loader
│   └── extensions/    animationextension, footballanimationextension
├── sim/             仿真核心（原 onthepitch）
│   ├── match, team, ball, referee, officials, humangamer, teamAIcontroller
│   ├── ai_support/     AIfunctions, mentalimage
│   ├── utils.*         QuantizeDirection / GetVelocityID 等游戏工具
│   └── player/
│       ├── player, playerbase, playerofficial, player_locomotion, *_collider,
│       │   player_action*, *_scheduler, player_kinematics, player_body_facing
│       ├── controller/  icontroller, playercontroller, humancontroller,
│       │                elizacontroller, refereecontroller, strategies/offtheball/*
│       └── humanoid/    humanoid, humanoidbase, humanoid_utils
├── env/             对外环境层
│   ├── game_env, gametask, main
│   ├── defines, gamedefines
│   └── match_setup, gfootball_actions.h
├── data/            matchdata, playerdata, teamdata（DB/序列化）
└── ai/              ai_keyboard, ihidevice.hpp
```

`data/` holds animation files (`.anim`), object models (`.ase`/`.object`), textures,
shaders, and team/player database files. Do not edit these casually; the animation
files are inputs to the regression baseline.

## Runtime data flow

```
main() [src/env/main.cpp]        thread_local GameEnv* game; Tracker tracker;
  → run_game()                   builds GameContext + GameTask + AIControlledKeyboard[]
  → GameEnv [src/env/game_env.*] start_game / reset(config) / step / action /
                                  get_info→SharedInfo / get_state / set_state
      → GameTask [src/env/gametask.*]::ProcessPhase()
          → Match [src/sim/match.*]::Process()  per-tick loop (10ms steps)
              → Ball::Process()                  physics + prediction buffer
              → Team → Player::Process()         Humanoid animation + controller strategy
              → Referee / Officials              rules, fouls, match phase
```

- `GameEnv` is the stable public API; `Match`/`Player`/`Humanoid` are internals.
- `physics_steps_per_frame` (default 10) subdivides each environment step.
- Determinism: the `boost` RNG was replaced with bit-identical `std::mt19937`
  (`GameContext::rng`). Never introduce unordered-container iteration order or
  hidden global mutable state into simulation logic.

## Conventions and gotchas

- **C++23**, extensions off, position-independent code on (see `CMakeLists.txt`).
- **Namespace**: `blunted::` for foundation math/geometry/types. The football layer is
  in the global namespace with `e_*` enums and plain classes.
- **Include guards**: legacy headers use `#ifndef _HPP_...` / `#define _HPP_...` style.
- **Validation**: `DO_VALIDATION` (a no-op unless `FULL_VALIDATION` is defined) hooks
  into `Tracker::verify`, which drives cross-process deterministic replay comparison.
  Leave it in place when editing.
- **No mirror state**: an authoritative field is stored once; derived values are
  accessors (e.g. `speed == velocity.GetLength()`), never a second field.
- **Determinism is the product.** Before and after any refactor, run the regression
  suite; golden snapshots must remain byte-identical unless the change is intentionally
  semantic (then regenerate with `--print-baseline` and call it out in the commit).
- **Headless boundary**: core may not depend on SDL/OpenGL/X11 or Boost — the
  `football_headless_core_guard` test enforces this.
- **Symmetry**: `Mirror()` and `reverse_team_processing` exist for testing left/right
  symmetry; changes to position/movement must respect them.

## Ongoing work / terminology

The active migration on this branch is **animation root-motion → explicit procedural
kinematics** for player movement. (The broader `REFACTOR_PLAN.md` architecture split —
state vs systems vs world — belongs to the frozen `main` branch, not this one.) Key files:

- `src/sim/player/player_kinematics.hpp` — explicit `PlayerKinematicState` +
  `PlayerKinematics::Step` (velocity/accel/braking/turn).
- `src/sim/player/player_locomotion.*`, `player_body_facing.hpp` — procedural
  locomotion producers.
- `src/sim/player/player_decision_scheduler.hpp`, `locomotion_intent_scheduler.hpp`
  — decision/locomotion cadence.
- `tools/4f-b-pure-locomotion-animation-read-audit.md` and
  `tools/5a1-contact-authority.md` — audit notes documenting what still reads
  animation state (action/contact authority) vs what has moved to kinematics.

Terminology used in commits/docs: "pure locomotion" (procedural `Movement` without
scheduled contact or retained ball), "smuggle offsets" (transitional animation-driven
position/rotation corrections), "root motion" (animation-keyframe-driven movement),
"contact authority" (Shot/Pass/Trap/BallControl still animate their contact frame).
When touching these areas, read the two audit notes first and keep unperturbed
regression output exact for measurement-only changes.
