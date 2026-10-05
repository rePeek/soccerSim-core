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

## Working rules

- **Structural changes must update AGENT.md.** Any commit that moves, renames, adds,
  or removes files/directories under `src/` (or changes build targets) must keep the
  "Source layout" tree and path references in this file in sync.
- **Commit locally, never push.** After finishing a change, stage it and commit it
  locally. Do **not** `git push` — remote pushes are done by the maintainer.

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
- `football_model_test` — STL-only domain-model checks (`tools/model_test.cpp`),
  linked solely to `football::model`; does not initialize a simulation or need data.
- `football_smoke` — minimal `GameEnv` headless run (`tools/football_smoke.cpp`);
  optional positional arg is the step count (default 1000).
- `football_headless_core_guard` — shell test (`tools/football_headless_core_guard.sh`)
  asserting `libgame.so` has no graphics `NEEDED` deps (SDL/GL/X11…) and that `src/`
  has no graphics include. **Do not add graphics or Boost dependencies to core.**

### Adding/removing source files

- `src/foundation/**` is owned by `src/foundation/CMakeLists.txt`; register new
  foundation files there. The root project consumes it with
  `add_subdirectory(src/foundation)`.
- Other `src/**/*.cpp`/`.hpp` files must be registered in `sources.cmake`.

## Source layout (current branch)

```
src/
├── model/           静态领域描述（football::model；只依赖 STL 与自身）
│   ├── ids.hpp        比赛内 PlayerId/TeamId 与独立的 PlayerDatabaseId
│   ├── player         PlayerStat / PlayerAttributes、姓名、年龄、身高与外观
│   ├── team           静态球队组成；不解析 DB、不生成默认资料
│   ├── formation      公开坐标下的初始阵型描述，不含 checkpoint/runtime 状态
│   ├── pitch          唯一场地几何值类型（当前保持 legacy 110 × 72 尺寸）
│   └── football_types e_PlayerRole/e_GameMode/e_PlayerColor/kPlayersPerTeam
├── foundation/      通用基础，依赖 DAG 的最底层（原 blunted base）
│   ├── math/           vector3, matrix3/4, quaternion, bluntmath, rng（纯算法）
│   ├── geometry/       line, triangle（aabb/plane/trianglemeshutils 已删）
│   ├── defines.hpp    仅通用宏/常量（CHECK/EPSILON）；不含足球领域枚举
│   ├── log, properties, utils, xml_loader, backtrace, file
│   └── misc/           hungarian（通用算法；perlin 已删）
├── animation/      runtime 动画系统（只依赖 foundation）
│   └── 运行时只读（baked schema + 选择器）：clip, library,
│       baked_selector, simanim_format, types, selection_*, quadrant
├── sim/             仿真核心（原 onthepitch）
│   ├── gamedefines.*   游戏常量（velocity/e_Velocity/e_FunctionType）
│   ├── simulation, match, match_config, team, ball, referee, officials, humangamer, teamAIcontroller
│   ├── ai_support/     AIfunctions, mentalimage
│   ├── utils.*         QuantizeDirection / GetVelocityID 等游戏工具
│   └── player/
│       ├── player, playerbase, playerofficial, player_locomotion, *_collider,
│       │   player_action*, *_scheduler, player_kinematics, player_body_facing
│       ├── controller/  icontroller, playercontroller, humancontroller,
│       │                elizacontroller, refereecontroller, strategies/offtheball/*
│       └── humanoid/    humanoid, humanoidbase, humanoid_utils
├── controller/      协议无关的控制输入接口，以及 GRF action 适配器
├── state/           对外运行时值快照（依赖 model 身份类型与 foundation 数学）
├── control/         PlayerControl / TacticalBoard 等协议无关控制契约
├── env/             对外环境层
│   ├── game_env, main, rng（全局 RNG 入口，owner 是 GameContext）
│   ├── model_adapter  静态模型 → legacy TeamCreationData / FormationEntry 转换
│   └── defines        EnvState / Position / SharedInfo
├── data/            legacy 资料导入与 runtime 兼容层
│   ├── player_profile  无 GameContext/RNG 的资料解析与年龄/能力计算
│   ├── default_teams   football::data 默认队伍工厂，返回完整 model::Team
│   ├── playerdata      持有 model::Player 的兼容 facade；无独立 stats/cache
│   └── matchdata, teamdata  runtime 组装与序列化
└── ai/              ai_keyboard, ihidevice.hpp
```

### 分层与边界守卫

已实现的下半段（能在 `CMakeLists.txt` 的 static library 边上看到）：

```text
model (叶) ← foundation ← animation ← data ← sim ← engine ← game
```

- `model` 只能 include STL 与自身；`model_boundary_guard` 防止 runtime/IO 依赖回流。
  `football_model` / `football::model` 接口库替代原 `football_domain`；不再存在
  `src/domain/`。模型可独立使用，`cmake -S src/model -B build/model-standalone`。
- `foundation` 只能 include STL 与自身。`foundation_boundary_guard` 强制这一点。
- `animation` 只能 include `foundation`（无上层 include）。
- `football_animation` 是独立 archive；`libfootball_foundation.a` 不含任何
  动画符号。

尚未清理的上半段（不要在此基础上新增反向边）：

- `sim/**` 与 `data/**` 仍 include `env/main.hpp`、`env/defines.hpp`。
- `env` 的 `GameContext` 仍同时持有 `SimulationRng` 状态与 checkpoint
  序列化，是历史遗留；长期应拆出 `SimulationState`。
- 全局 RNG 入口（`boostrandom`/`randomseed`/`random_non_determ`）声明在
  `env/rng.hpp`，因为它们需要选择 context 持有的 generator；算法本体
  （`Rng`/`SimulationRng`/`PresentationRng`）在 `foundation/math/rng.hpp`。
  确定性的 `SimulationRng` 状态属于 simulation checkpoint，只有 simulation
  代码可以抽取；多抽一次就会改变之后所有随机序列。

机械守卫（CTest，改坏边界会直接失败）：

- `model_boundary_guard`：禁止 model include 其他项目层，禁止环境全局入口、
  checkpoint 与文件格式/IO 实现。
- `foundation_boundary_guard`：禁止 foundation include 上层，也禁止出现
  `GetContext`/`EnvState`/`boostrandom`/`randomseed`/`random_non_determ`。
- `runtime_animation_boundary_guard`：禁止 runtime 依赖离线 `.anim` 管线。
- `football_headless_core_guard`：禁止 graphics/Boost 依赖回流。
- `legacy_validation_guard`：禁止已经删除的逐语句 validation 宏/函数回流。

`tools/animBaker/` holds the offline source-animation pipeline: the baker
entry point and guards, plus legacy `animation/`, `animcollection/`, import
hierarchy/loader and animation extensions. It is linked by
`football_anim_baker` and the legacy validation paths in
`football_regression`, never by `libgame.so`.
`foundation/xml_loader.*` is shared infrastructure for runtime data and the
offline importer; runtime animation itself does not parse XML.

`data/` holds animation files (`.anim`), object models (`.ase`/`.object`), textures,
shaders, and team/player database files. Do not edit these casually; the animation
files are inputs to the regression baseline.

## Runtime data flow

```
main() [src/env/main.cpp]        thread_local GameEnv* game;
  → run_game()                   builds GameContext + Simulation + AIControlledKeyboard[]
  → GameEnv [src/env/game_env.*] start_game / reset(config) / step / action /
                                  get_info→SharedInfo / get_state / set_state
      → Simulation [src/sim/simulation.*] owns match lifecycle; env builds MatchConfig
          → Match [src/sim/match.*]::Step()         per-tick loop (10ms steps)
              → Ball::Process()                  physics + prediction buffer
              → Team → Player::Process()         Humanoid animation + controller strategy
              → Referee / Officials              rules, fouls, match phase
```

- `GameEnv` is the stable public API; `Match`/`Player`/`Humanoid` are internals.
- Static composition uses `football::model::Team`, `Player`, `Formation` and
  `Pitch` (`src/model/`). `GameEnv(home, away, pitch)` retains these descriptions
  across resets; no `MatchSetup`/`TeamSetup`/`PlayerSetup` layer remains. The default
  `GameEnv()` still supports legacy ScenarioConfig-only callers. Non-empty scenario
  formations override model positions without replacing the retained roster.
- `model::Player` owns its static identity, appearance and all 22 base abilities.
  `PlayerStat` and `PlayerAttributes` live together in `src/model/player.hpp`.
  `PlayerDatabaseId` is provenance, not a request to reload values at startup; it
  remains distinct from match-local `PlayerId`. Fatigue and actions stay runtime.
- `football::data::LoadLegacyPlayerProfile` (`data/player_profile.*`) resolves old
  profiles without a game context or RNG, preserving legacy decimal round-tripping
  at the import boundary. Explicit model attributes are consumed without rounding.
  Default team factories now live in `data/default_teams.*`, not the model.
- `PlayerData` is a legacy facade owning one model, with no second ability array or
  cached velocity. Runtime initialization still consumes the historical one skin
  colour RNG draw per player/official, even for explicit appearances, to preserve
  simulation RNG order. Missing skin colour is resolved only at this boundary.
- `model::FormationEntry` is an initial declaration in public pitch coordinates.
  The legacy global `FormationEntry` is a separate role-adapted runtime/checkpoint
  representation; conversion and coordinate scaling live in `env/model_adapter.*`.
- `model::Pitch` is the sole pitch value type. `MatchConfig` passes it by value and
  `Match` owns a read-only copy. Only legacy geometry is supported for now.
  Transitional constants in `gamedefines.hpp` derive from that same geometry.
  Configurable dimensions require migrating remaining consumers and scaling.
- `physics_steps_per_frame` (default 10) subdivides each environment step.
- Determinism: the `boost` RNG was replaced with bit-identical `std::mt19937`
  (`GameContext::rng`). Never introduce unordered-container iteration order or
  hidden global mutable state into simulation logic.

## Conventions and gotchas

- **C++23**, extensions off, position-independent code on (see `CMakeLists.txt`).
- **Namespace**: `football::model` for static domain descriptions and identities;
  `blunted::` for foundation math/geometry/types. Legacy runtime classes and `e_*`
  enums remain global for now; model construction must not depend on them through
  simulation or environment headers.
- **Include guards**: legacy headers use `#ifndef _HPP_...` / `#define _HPP_...` style.
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
state vs systems vs world — belongs to the frozen `main` branch, not this one.)

The animation pipeline is fully baked: the runtime reads only
`assets/runtime/animations.simanim` via `AnimationLibrary`/`BakedAnimationSelector`;
the legacy `.anim`/XML/`player.object` parsing lives only in `football_anim_baker`
(`football_legacy_anim` target, not linked into `libgame.so`). Key files:

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
