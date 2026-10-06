# AGENT.md

Guidance for AI agents working in this repository.

## What this project is

`soccerSim-core` is a headless **football (soccer) simulation core** written in C++23.
It is a heavily modified fork of
[BazkieBumpercar/GameplayFootball](https://github.com/BazkieBumpercar/GameplayFootball),
which itself descends from Google Research Football and the `blunted2` engine.

The engine is deterministic, animation/physics driven, and exposes a core `GameEnv`
API: explicitly declared teams/pitch, start/reset/stop, one 10 ms tick per `step()`,
controls and `WorldState` observations. The GRF environment adapter is removed.

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

Regression fixture imports need a test-side data directory (the runtime no longer
reads `GFOOTBALL_DATA_DIR`; it loads the configured baked animation asset):

```sh
export GFOOTBALL_DATA_DIR="$PWD/data"
```

`build/` is git-ignored. If a preset configure behaves unexpectedly after a large
restructure, reconfigure from scratch with `rm -rf build/<preset>`.

Catch2 is the unit-test framework for the `test/` suites and is fetched by
[CPM](https://github.com/cpm-cmake/CPM.cmake) (`cmake/CPM.cmake`, currently
Catch2 v3.7.1). `CPM_SOURCE_CACHE` is pinned to `.cache/CPM`, which is already
git-ignored, so reconfiguring never re-downloads the checkout. The fetch happens
only under `BUILD_TESTING`; `-DBUILD_TESTING=OFF` (and any library-only consumer)
configures without network access.
`-DFOOTBALL_BUILD_APP=OFF` additionally skips the CLI executable and its sample
inputs (`src/app/`); the core shared library never links them either way.

### Test targets

- `football_regression` — core simulation regression (`tools/football_regression.cpp`):
  preserved independent kinematics, body facing, locomotion, collider, scheduler
  and command-adapter checks; 100 Hz raw WorldState goldens, authoritative-state
  and RNG reset/replay checks, direct Simulation diagnostics, model ownership,
  canonical frames, cached-animation lifetime and offline import
  fixtures; rule-only foul/card/restart timing, penalty, advantage and offside
  tests; concrete/non-polymorphic Player, difficulty/fatigue-adjusted stats and
  deactivation/teardown RNG-window checks. Animation A/B branches reset/replay
  rather than load a checkpoint.
  `--print-baseline`, `--animation-ab`, `--animation-ab-lifecycle` remain available.
  Only offline import fixtures require `GFOOTBALL_DATA_DIR`. The former GRF
  projection/cadence, episode-override and legacy checkpoint assertions and
  GRF-specific telemetry were retired with that interface. These new core goldens
  intentionally do not represent the removed 100 ms GRF observation contract.
- `football_model_test` — STL-only domain-model checks (`tools/model_test.cpp`),
  linked solely to `football::model`; does not initialize a simulation or need data.
- `football_game_env_test` — core API checks (`tools/game_env_test.cpp`): no default
  constructor or legacy surface, one-tick stepping, reset/restart determinism,
  retained observations, copied abilities/height/IDs across reset/restart, control
  isolation, independently stepped live environments, peer teardown and rejected
  startup (no half-initialized runtime is published).
- `football_player_identity_test` — direct Simulation checks with no environment
  binding (`tools/player_identity_test.cpp`): sparse/full-width IDs, model-ID
  controls, identity-independent numerical/RNG replay, reversed construction,
  unequal rosters, reorder/side changes, send-offs, and rosters beyond 256 entries;
  policy-versioned tactical/reachability/RNG fingerprints (`--print-baseline`) and container-ordered human
  controller selection (including repeated phases and descending IDs). Invalid
  or duplicate IDs/missing profiles/empty rosters fail before RNG consumption.
- `football_default_ai_test` — eight value-policy cases; links only ai/contracts/Catch2,
  not sim or game. Persistent intent/replay, roster ties, eligibility, external edits,
  independent AI ownership, restart authority and retention release.
- `football_sim_control_boundary_test` — eight cases: AI-free execution, frame/ID
  translation, snapshots, tactical lifetimes, hands legality, actual restarts and
  Human/explicit priority; original-source placement/RNG baselines cover 72 cases.
  See CTest for the current complete test count.
- `football_smoke` — core `GameEnv` headless run (`tools/football_smoke.cpp`);
  optional positional arg is the simulation tick count (default 1000).
- `football_headless_core_guard` — shell test (`tools/football_headless_core_guard.sh`)
  asserting `libgame.so` has no graphics `NEEDED` deps (SDL/GL/X11…), that `src/`
  has no graphics include, simulated official actors or animation-driven restart
  timing hook, and no retired environment binding in runtime/diagnostics/symbols.
  It also rejects `app/fixtures` includes/symbols and the retired `src/data` layer
  in core.
  **Do not add graphics or Boost dependencies to core.**

### Adding/removing source files

- `src/foundation/**` is owned by `src/foundation/CMakeLists.txt`; register new
  foundation files there. The root project consumes it with
  `add_subdirectory(src/foundation)`.
- Other `src/**/*.cpp`/`.hpp` files must be registered in `sources.cmake`
  (`GAME_*` for the core, `APP_SUPPORT_*`/`APP_MAIN_SOURCES` for the executable).
- Executable-side C++ sources live in `src/app/` and are collected into the
  `EXCLUDE_FROM_ALL` `football_app_support` archive; the core shared library never
  links them. Unit tests live in `test/` and are registered in
  `test/CMakeLists.txt` with Catch2 `TEST_CASE`/`SECTION` syntax.

## Source layout (current branch)

```
src/
├── model/           静态领域描述（football::model；只依赖 STL 与自身）
│   ├── player         PlayerId（外部 identity）、legacy PlayerDatabaseId、能力与外观
│   ├── team           静态球队组成、typed tactics 与 TeamSide；无 TeamId/DB/隐式默认
│   ├── formation      公开坐标下的初始阵型 + 归一化比例的 typed TacticalFormation
│   ├── pitch          唯一场地几何值类型（当前保持 legacy 110 × 72 尺寸）
│   └── football_types e_PlayerRole/e_GameMode/e_PlayerColor/kPlayersPerTeam
├── foundation/      通用基础，依赖 DAG 的最底层（原 blunted base）
│   ├── math/           vector3, matrix3/4, quaternion, bluntmath, rng（纯算法）
│   ├── geometry/       line, triangle（aabb/plane/trianglemeshutils 已删）
│   ├── defines.hpp    仅通用宏/常量（CHECK/EPSILON）；不含足球领域枚举
│   └── algorithm/      hungarian（通用算法；perlin 已删）
├── support/         通用配置/IO/诊断/codec（只依赖 foundation）
│   ├── config/properties, diagnostics/log + backtrace
│   └── io/file + xml_loader, text/string_utils + value_codec
├── sim/             仿真核心（原 onthepitch）
│   ├── animation/      runtime 动画子系统（只依赖 foundation；见下方 archive 说明）
│   │                   运行时只读（baked schema + 选择器）：clip, library,
│   │                   baked_selector, simanim_format, types, selection_*, quadrant
│   ├── gamedefines.*   游戏常量（velocity/e_Velocity/e_FunctionType）
│   ├── simulation, match, match_options（仅规则参数）, team, ball, referee（规则）, humangamer
│   ├── team_tactical_state  Human/team run/pressure/rush 请求的权威计时与目标
│   ├── formation       role adaptation / personal-space 归一化（纯算法，无 DB 查找）
│   ├── query/          player_query（空间/控球/切人查询）, reachability（运动学估算）
│   ├── rules/          offside（规则几何）, restart_placement（重开球定位/选择）
│   ├── ai_support/     mentalimage（Match-owned 历史；未改动）
│   └── player/
│       ├── player（具体 runtime；仅模型 PlayerId 与私有 schedule_phase_，无 runtime index）, player_locomotion, *_collider,
│       │   player_action*, *_scheduler, player_kinematics, player_body_facing,
│       │   ball_approach（Human 追球/控球辅助）, kick_targeting（传射执行计算）
│       ├── controller/  playercontroller（仅 Human 使用的 command planner，无 AI 基类）,
│       │                humancontroller（输入→legacy command 适配）
│       └── humanoid/    humanoid, humanoidbase, humanoid_utils
├── ai/              值决策层（football_ai，只依赖 observation/control 及其叶契约）
│   ├── tactical_board  AI-owned persistent desired/planned configuration（不属于 sim/control）
│   └── default_ai      持有双方 boards；只读 WorldState/意图，输出 PlayerControlSet
├── controller/      协议无关控制输入接口（controller_input/external_controller）
├── observation/     对外观测契约：WorldState / WorldPlayerState / WorldTeamState 值类型
│                    （只依赖 model 身份类型与 foundation 数学；不含观测逻辑）
├── control/         PlayerControl / PlayerControlSet 协议无关仿真输入契约
├── env/             对外环境层（组合根，允许依赖 sim 与 ai）
│   ├── default_ai_setup 从静态球队描述一次性 bootstrap AI；复用 sim 的纯 formation 算法
│   └── game_env        持有 Simulation 与持久 DefaultAI；tactics(side) 转发 AI 的 value API
└── app/             可执行文件侧（football_app_support），不进入 core .so
    ├── app.cpp        CLI 入口
    ├── args           `--steps=N` 参数解析（可单独测试）
    └── fixtures/      legacy 默认队伍/球员资料 → model::Team/Player（仅 CLI 与测试）
        ├── legacy_player_profile
        └── default_teams
```

Repository-root extras (tests only; never part of the core library):

```text
cmake/
└── CPM.cmake        vendored CPM bootstrap; Catch2 is fetched through it

test/                 Catch2 suites and architecture guard negative tests
├── app_args_test     CLI argument parsing
├── app_fixtures_test default teams and legacy profile import
├── app_cli_test      the GameEnv composition the CLI builds
├── sim_computation_test reachability baselines, query ordering, offside, kick mechanics
├── default_ai_test      只链接 AI/值契约，决策不依赖 actor/runtime
├── sim_control_boundary_test  controls/帧/规则/计时/Human
├── restart_placement_test + fixture  原实现独立捕获的 72-case 精确 baseline
├── default_ai_fixture   诊断自己的 observe → AI → controls → step 组合根
├── baselines/pre_value_ai.md  替换默认策略前的历史黄金值
└── module_dependency_guard_test.sh reverse-edge/obsolete API rejection
```


The unused speculative `src/ai/player/player_ai.hpp` remains deleted. Real default
decisions now consume values in `default_ai.*`; no actor-calling decision interface
or factory survives. External policies may observe/control Simulation directly.

The retired `src/data/` layer is deleted, not moved: `PlayerData`, `TeamData`,
`MatchData` and `model_adapter` are gone. `Player` holds `const model::Player&`,
`Team` owns a `model::Team` copy plus a derived `FormationEntry` vector and
`Properties` tactics, and `Match` owns score/possession runtime state.
`src/app/fixtures/` supplies legacy sample rosters for the CLI and tests; it may
include `support`, and nothing in core may include `app/` or reference its symbols.

### 分层与边界守卫

已实现的下半段（能在 `CMakeLists.txt` 的 static library 边上看到）：

```text
runtime:  model/foundation (叶) → sim/animation → sim → engine/env → game
policy:   model/foundation → observation/control → ai → engine/env → game
exec:     app fixtures/importers → model + support（仅 executable，不进 .so）
```

`football_app_support` (the `EXCLUDE_FROM_ALL` archive holding `src/app/`) and
the Catch2 suites in `test/` sit strictly above this DAG: they may use the core,
but no core target links them. The only third-party dependency (Catch2, via CPM)
is fetched under `BUILD_TESTING` and is never a core dependency either.

Every target compiles with `-I src` (headers cross-reference `module/...`), so a
missing link edge between two header-only contracts still compiles. The declared
edges are therefore documentation, and `module_dependency_guard` is what keeps
them true: CMake exports each module target's real transitive closure into
`module_dependencies.txt` and the guard fails when a module includes a prefix its
target does not declare. "Declared but unused" edges are not reported (the
compiler cannot see them either); removing one stays a review item.

- `model` 只能 include STL 与自身；`model_boundary_guard` 防止 runtime/IO 依赖回流。
  `football_model` / `football::model` 接口库替代原 `football_domain`；不再存在
  `src/domain/`。模型可独立使用，`cmake -S src/model -B build/model-standalone`。
- `foundation` 只能 include STL 与自身。`foundation_boundary_guard` 强制这一点。
- `sim/animation` 只能 include `foundation`（无上层 include）。
- `football_animation` 仍是一个独立 archive（源码在 `sim/animation/`）：它只依赖
  foundation，离线 baker 因此能在不链接整个仿真的前提下 load/verify 它烤出的
  `.simanim`。`libfootball_foundation.a` 不含任何动画符号。

尚未清理的上半段（不要在此基础上新增反向边）：

- `sim/**` 不 include `env/**`；`model`/`foundation`/`sim`/`env`/
  `observation`/`control`/`controller`/`support` 不 include `app/`、不引用 fixture
  符号；`src/data/` 与顶层 `src/animation/` 不再存在。整个 runtime 已无环境全局入口。
- `controller/**` 与 HumanGamer/HumanController 的 legacy 输入簇仍待单独审计；
  不要在环境所有权工作中删除仍有 reader 的 ControllerInput。
- `env/rng.*` 已删除；确定性的 `SimulationRng` 由 `Simulation` 持有，算法本体在
  `foundation/math/rng.hpp`。没有全局 RNG 入口；正常重构不得额外抽取随机数。


### 决策层迁移（Phase 1–5 已完成）

**不变式：sim 永不 include/link ai。** 默认 AI 仍在 `libgame.so`（A 模式）；
部署打包与依赖方向独立。Phase 5 进一步让 AI 不再 include/link sim/actor runtime。

```text
WorldState + TacticalBoard → default player policy → PlayerControlSet → Simulation
```

**1–3（历史，bit-exact）**：用临时 player/team 决策端口倒转依赖，真实 Eliza /
TeamAI / factories / strategies 搬入 ai；随后彻底删除 `AIfunctions.*` 分类，按
policy、query、rules、mechanics 的语义归属拆分。旧端口不是终局接口。

**4：回收权威状态。**

- `RefereeBuffer` 是 restart type/taker/prepare/start 的唯一权威来源；不再在 AI
  中缓存第二份 set-piece 状态。`sim/rules/restart_placement.*` 负责原来的定位、
  taker 选择和 retain 动画；Referee 按原顺序/时钟/reseed 调用。Team 的 Human
  便利查询只读 Referee，不保存 restart 状态，也不调用决定对象。
  新 restart 的 pending 阶段清空旧 taker，直到 prepare 才发布本次 taker。
- `Team` 持有 `TeamTacticalState`：attacking run（4000 ms）、pressure（500 ms）、
  keeper rush（300 ms）的 deadline/目标。Human 输入发起请求，ResetSituation
  清理；过期/送下场不把 actor 或 timer 留在 AI。WorldState 发布剩余 ms 与实际 marking。
  pressure 的临时 Human marking 在替换/到期时清理，观测也按同一 deadline 截止；
  不依赖已删除的周期 AI marking 来收尾。
- 六种 restart × 两种 processing order × 两个 taker team × 三种 roster
  （11/11、3/2、1/1）共 72 case，与独立编译的 7dd3c63 原 PrepareSetPiece +
  formation policy 逐 case 比较浮点/动作/retain/taker/RNG，完全一致。捕获结果
  固定在 `test/restart_placement_test.cpp`。裁判时钟/card/send-off 规则未改。

**5：实际值边界，而非 legacy 包装。**

- `ai/default_ai.*` 持有 `std::array<TacticalBoard, 2>`，提供 `tactics(TeamSide)`
  的 mutable/const value API 与 `Update(WorldState, output)`。`ai/tactical_board.hpp`
  只含意图（role、base formation anchor、desired marking、width/depth）；没有 restart
  taker、当 tick press/run/rush 或计时。Update 计算局部 target，绝不改写 board。
  width/depth 保持米制尺寸（0 选默认 75%/55%）；formation_position 是 home-frame 米制
  base anchor，不是每 tick 的 adapted target。
  没有 Match/Team/Player/MentalImage 指针、回调、隐藏 RNG 或 actor cache。
- `env/default_ai_setup.hpp` 在组合根按静态 model descriptions 初始化 shape 一次；
  复用 sim/formation 的 legacy 纯算法以保持默认策略，不读取 runtime/snapshot。
  直接使用 DefaultAI 的调用者也可自己提供初始 boards，无需链接 sim。
- `GameEnv::step()`：Observe → persistent DefaultAI::Update → merge 显式 controls
  → 一次 Simulation::Step（10 ms）。AI 不覆盖活跃 Human；显式 control 优先。
  `controls()` 不缓存 AI 输出。GameEnv::tactics(side) 在启动前也可修改；sim reset/
  stop/start 保留战术配置，只有 AI/environment 的销毁或显式覆盖会替换它。
- `Simulation::ObserveTactics()` 与 Team 的同名 API 已删除，无兼容 shim；sim、
  observation、control 完全不知道 TacticalBoard。暂不引入 CoachControl/CoachAI。
- `Simulation()` 现在可默认构造，因为完全没有决定对象。构造/Match/Player/Team
  不接收 factory/decision。空 controls = idle movement（或已有 Human 输入），
  不是仿真私自运行 AI。`test/default_ai_fixture.hpp` 是诊断自己的值组合根。
- 删除全部 `Legacy*Decision*` / factories、Eliza、TeamAIController、旧 strategies
  及不再使用的 actor-based policy helper。没有 alias、compat factory 或新
  ITeamController。`football_ai PUBLIC football_observation football_control`；
  `football_sim` 不含 AI，AI target 也不含 sim/support/controller/animation/engine。
- WorldState 仍是 owning values、不是 simulation storage：补齐 ball velocity、
  pitch、双方 score/defending direction、play/set-piece、restart type/taker、
  ball retainer、actor Human ownership/lazy/max speed、run/pressure/rush 剩余 ms 与
  actual marking target；positions/velocities/facing
  和 controls 使用同一 home pitch frame。Builder 在实际消费时转换 processing
  frame，按模型 ID 解析活跃队友，传射用 sim mechanics 算方向/力度/抛物线。
  移动/视线投影到平面；动作命令有机械 movement fallback 来维持 reset/re-entry
  intent。非法 hands save 和失活 recipient 在 execution 侧拒绝。
  TacticalBoard ownership 重构保留默认 simulation digest/轨迹/RNG；World hash 因显式
  纳入新增请求字段而更新（仅观测 schema 的覆盖变化，不是策略基线重捕获）。
- `HumanController` / `HumanGamer` 保留 sim input/selection ownership；其
  `PlayerController` planner 不再继承决定端口，仅 Human 使用。`MentalImage`
  仍是 Match-owned 执行历史；Player 的 execution reaction/cache 不是 AI 对象。
  Humanoid/HumanoidBase 不 flatten，动画 cache、10 ms step、private phases、
  reset/destruction 次数与 appearance/reseed 窗口保持原机制。

**明确的语义变化**：默认策略被真正替换为较小的确定性值策略（位置/追球、传射、
接球/盘带、keeper 与 restart），不是无损搬运 Eliza 的多命令/动画反馈策略。
AI 现在每 tick 从前一完整快照决策，不再抽取旧策略 RNG；不补 dummy draws。
轨迹、policy/scheduling 黄金值与 A/B event ticks 已重新捕获，旧值保存在
`test/baselines/pre_value_ai.md`。World hash 也显式枚举新增字段。不要声称 Phase 5
bit-exact，也不要为了旧策略基线重建 runtime 指针/命令回调。

通用计算继续在 sim：query/reachability、rules/offside、mechanics/ball_approach
与 kick_targeting；不是因为 AI 不再调用就变成 AI policy。私有 pass direction/
power 算法仍活跃，没有重建 `AI_` 公共 API。`module_dependency_guard` 同时检查
实际 include 前缀和 CMake closure；拒绝 sim → ai、AI → actor/runtime 的直接或
传递边、退役决定对象/file 与 AIfunctions/AI_ 回流。负例只修改临时树。

机械守卫（CTest，改坏边界会直接失败）：

- `model_boundary_guard`：禁止 model include 其他项目层，禁止环境全局入口、
  checkpoint 与文件格式/IO 实现。
- `foundation_boundary_guard`：禁止 foundation include 上层，也禁止出现
  `GetContext`/`EnvState`/`boostrandom`/`randomseed`/`random_non_determ`。
- `runtime_animation_boundary_guard`：禁止 runtime 依赖离线 `.anim` 管线。
- `football_headless_core_guard`：禁止 graphics/Boost、裁判 humanoid actors、
  已删除的 PlayerBase/PlayerIndex/GetIndex、ambient numbering、model/ids.hpp 与动画重开球 timing hook
  回流；同时禁止整个 runtime/内部诊断/共享库符号出现已删除的环境绑定，
  并禁止 core 出现 `app/fixtures` 依赖或 `src/data` 回流。
- `legacy_validation_guard`：禁止已经删除的逐语句 validation 宏/函数回流。
- `module_dependency_guard`：把 CMake 导出的真实 target 闭包与 `src/<module>/` 实际
  include 的项目前缀（含 `ai/`）对比；未声明的依赖直接失败，并无条件禁止 sim → ai
  link closure、AI → actor/runtime closure、retired decision API/file 以及 AIfunctions/AI_ 回流。
  同时拒绝 TacticalBoard/PlayerDirective/ObserveTactics 泄漏到 sim/observation/control；有独立负例测试。
  `src/app` 与 `tools/` 不在范围内：它们位于 core 之上，可使用任意 core 模块。

`tools/animBaker/` holds the offline source-animation pipeline: the baker
entry point and guards, plus legacy `animation/`, `animcollection/`, import
hierarchy/loader and animation extensions. It is linked by
`football_anim_baker` and the legacy validation paths in
`football_regression`, never by `libgame.so`.

Two different include roots share the `animation/` name, and they are not the
same thing:

- `sim/animation/...` is the **runtime** baked-animation reader (owned by sim).
- `animation/...` is the **offline legacy** pipeline, rooted at `tools/animBaker/`
  (names: `animation.hpp`, `animcollection.hpp`, `import_*`, `extensions/*`).

The filename sets are disjoint, so an include prefix identifies its root without
ambiguity. The baker intentionally reuses the runtime `AnimationLibrary`/
`BakedAnimationSelector` to load and verify the artifact it just wrote; that is
why it links `football_animation` while still avoiding the simulation.

`support/io/xml_loader.*` is shared infrastructure for executable fixture/profile
and offline importers; runtime animation itself does not parse XML.

The repository-root `data/` directory (distinct from the deleted `src/data/` layer)
holds animation files (`.anim`), object models (`.ase`/`.object`), textures,
shaders, and team/player database files. Do not edit these casually; the animation
files are inputs to the regression baseline.

## Runtime data flow

```
main() [src/app/app.cpp]
  → GameEnv(home, away, pitch) [src/env/game_env.*]
      start_game / reset_game / stop_game / step / controls / observe→WorldState
      → WorldState + AI-owned TacticalBoard → persistent DefaultAI → merged PlayerControlSet
      → private unique_ptr<Simulation> [src/sim/simulation.*]
          owns team descriptions, match lifecycle, RNG and baked-animation cache
          → Match [src/sim/match.*]::Step()         per-tick loop (10ms steps)
              → Ball::Process()                  physics + prediction buffer
              → Team → Player::Process()         execution / Humanoid / Human input
              → Referee                         rules, fouls, match phase
```

- `GameEnv` is the stable public API; `Match`/`Player`/`Humanoid` are internals.
  `getObservations()` declarations have been removed. Stack-trace installation and
  console formatting belong to application/tool entry points, never to
  `GameEnv::start_game()`.
- Static composition uses `football::model::Team`, `Player`, `Formation` and
  `Pitch` (`src/model/`). `GameEnv(home, away, pitch)` requires explicit descriptions
  and retains them across reset/restart. There is no default `GameEnv()`, public
  runtime state, public episode configuration or startup/composition wrapper.
- `GameEnv::init_match(Simulation&)` starts a match from retained team/pitch
  descriptions with default `MatchOptions`. Startup initializes a local Simulation
  before publishing ownership; validation failure leaves the environment stopped.
  Reset keeps the same Simulation (and its RNG/library), releases Match and
  clears controls before Init. Stop destroys Simulation and clears controls;
  stop/start creates a fresh seeded runtime and animation library.
- `GameContext`, `GetContext`/`GetGame`/`SetGame`, `ContextHolder`, `run_game`/
  `quit_game`, dead `e_RenderingMode`/`GameState` and `env/main.*` are deleted.
  There is no active environment, thread-local binding or compatible accessor.
  Internal regression diagnostics explicitly own/pass Simulation; raw goldens
  also compare a separate GameEnv solely through its public API.
- The GRF compatibility adapter is deleted completely: no substitute shim, test
  support adapter, duplicate owner or public cadence/checkpoint API. Regression
  uses direct Simulation fixtures and public GameEnv/WorldState assertions.
- Simulation validates explicit team descriptions (ids, roster/formation sizes),
  resolves missing appearance, applies the RNG seed, then creates match actors.
  Preserve that order: the appearance `Uniform(1,4)` draws consume RNG before
  reseeding. There is no episode-config argument: the match rules are the
  `MatchOptions` defaults, snapshotted by `Match`.
- `sim/match_options.hpp` is the only match-rule input: time/rule/difficulty
  values, the kickoff ball position and the two booleans derived from the
  effective initial formations (`left_team_owns_ball`, `dynamic_player_selection`).
- `Simulation::Init` and `Match` no longer take or store a controller registry.
  `Match::UpdateControllerSetup` and `Match::controller_assignments_` are deleted.
  Players consume the supplied values first (`Player::RequestCommand`), otherwise
  Human input if active, otherwise idle. Only GameEnv/diagnostic composition runs AI.
- The checkpoint serialization layer is deleted completely: `EnvState`, every
  `ProcessState`/`ProcessStateBase`, the old controller registry and
  `AIControlledKeyboard` are gone, together with `env/defines.*` and
  `ai/ai_keyboard.*`. No byte-blob save/load remains; `GameEnv` never exposed it
  and regression uses reset/replay instead. If durable save/load is ever needed,
  add an explicit state value object, not per-class `memcpy` hooks.
- `model::Player::id` is the caller-provided external identity. `PlayerId` and
  `kInvalidPlayerId` live beside Player in `model/player.hpp`, not a horizontal
  IDs module. `PlayerDatabaseId` is retained there as a legacy GRF import key;
  profile loading never derives a PlayerId from it. Abilities/appearance have
  the same authoritative model ownership. Fatigue/actions remain runtime.
- `model/ids.hpp`, `TeamId` and `kInvalidTeamId` are deleted. `model/team.hpp`
  defines `TeamSide::Home/Away`: a match role that never flips on a pitch mirror
  or change of ends, not stable club identity. `WorldPlayerState::side` and
  `TacticalBoard::side` use it. Legacy rule-engine Team::GetID remains an integer
  slot (0/1); no model::Team::id or replacement generic IDs header is introduced.
- `Player::GetID()` reads `model_.id` directly. There is no
  second runtime identity, ID remapping table or runtime numbering allocator.
  `PlayerIndex`, `index_` and `GetIndex()` are deleted, not aliased or renamed.
- Roster order is container information. Human-controller selection filters
  the closest subset by walking Team's original roster, with no ID/phase sort
  and no stored roster slot. Ordinary vector/array positions remain local indices.
- Player's private `const uint8_t schedule_phase_` is only a non-unique 0..9
  stagger for tactical/reachability work. Match supplies the first roster's
  count modulo 10 for the second roster (including reversed processing); Team
  cycles phases during creation. It has no getter and never enters model,
  controls or WorldState. Send-offs/inactivity do not compact surviving phases.
  The 100 ms phase cadence remains; Phase 5 intentionally versions policy/RNG fingerprints.
- `Simulation` requires explicit rosters and rejects a `model::Team` with no
  players instead of resolving a database default. It validates duplicate or
  invalid ids and formation/profile sizes first, then consumes one skin-colour
  `Uniform(1,4)` draw per declared profile (including profiles omitted by a
  smaller formation and profiles with explicit appearance), then reseeds, then
  builds actors. IDs are never generated or remapped by simulation. Tests
  exercise 260 entries including inactive players; the artificial 256-actor
  capacity check is removed with `PlayerIndex`. Sample factories in
  `app/fixtures/default_teams.*` provide disjoint IDs 0..10 / 11..21.
- `Player::Mirror` skips never-activated bench entries, which have no Humanoid
  pose. Active and sent-off players still own their Humanoid and retain the
  original mirror/oracle path. This fixes the old null-pose bench dereference
  without flattening Humanoid or changing supported-player numerical behavior.
- `football::app::fixtures::LoadLegacyPlayerProfile`
  (`app/fixtures/legacy_player_profile.*`) resolves old profiles without a
  simulation, preserving legacy decimal round-tripping at the import boundary.
  Explicit model attributes are consumed without rounding. Default team factories
  live in `app/fixtures/default_teams.*`, never in the model or the core library.
  `PlayerData`/`TeamData`/`MatchData` are deleted: `Player` reads abilities, height
  and identity from the `model::Player` owned by `Team`, and `Match` owns score and
  possession runtime state directly. Missing skin colour is resolved once in
  `Simulation::Init`, with no facade or dummy compatibility draws.
- `model::FormationEntry` is the initial declaration in public pitch coordinates.
  The legacy global `FormationEntry` is the role-adapted runtime representation.
  `sim/formation.*` builds it from `model::Team` typed data (algorithm only, no DB
  lookup or XML); `Simulation::Init` converts the declared formation directly.
  There is no episode override, and `src/data/model_adapter.*` is deleted.
- `model::Pitch` is the sole pitch value type, passed directly to simulation.
  `Match` owns a read-only copy. Only legacy geometry is supported for now.
  Transitional constants in `gamedefines.hpp` derive from that same geometry.
  Configurable dimensions require migrating remaining consumers and scaling.
- Core `GameEnv::step()` calls `Simulation::Step` exactly once (10 ms). There is no
  `physics_steps_per_frame`, batching helper or observation scaling on its API.
  Callers batch explicitly with a loop. `observe()` returns unscaled home-frame WorldState values.
  The CLI `--steps=N` now means N simulation ticks, not N legacy 100 ms frames.
- Baked animations are owned explicitly: `Simulation` loads the library once and
  shares it with each `Match`, which exposes `GetAnimationLibrary()` for
  `Humanoid`/`HumanoidBase` and `Match::GetAnimPositionCache`. No actor uses
  an ambient environment to obtain animations; Stop keeps this cache until
  its Simulation owner is destroyed.
- `ScenarioConfig` and `GetScenarioConfig()` are deleted. Tick-time readers
  (`Referee`, `Team`, `Match::Step`) use `Match::options()`;
  the legacy derived helpers became the `left_team_owns_ball` /
  `dynamic_player_selection` fields computed once in `Simulation::Init`.
- The deterministic simulation RNG is owned by `Simulation` (`rng_`), seeded to 0
  `Simulation::Init` appearance draws, exactly as the legacy startup did. `Match`
  holds a reference and exposes `Match::rng()`; every former `boostrandom()` call
  site now draws through the `Match`/`Player` it already has, so `boostrandom()`
  is deleted. Preserve that seed window: reseeding before the appearance draws
  would change every downstream draw.
- `stablePlayerCount`, `stable_id` and `GetStableID` are deleted, not relocated.
  Neither simulation/data nor environment lifecycle has an ambient binding.
  The dead presentation RNG channel (`env/rng.*`, `PresentationRng`, `randomseed`,
  `random_non_determ`, `randomize()` and C `rand()`) remains deleted.
  The internal `animations` flag affects referee restart timing, not just
  rendering; its false/default behavior is preserved. Fixture paths are test-only.
- Determinism: the `boost` RNG was replaced with bit-identical `std::mt19937`
  (`Simulation::rng_`). Never introduce unordered-container iteration order or
  hidden global mutable state into simulation logic.
- Simulated officials are removed: `Officials`, `PlayerOfficial`,
  `RefereeController`, `Match::GetOfficials`/`GetOfficialPlayers` and the actor
  reset/process paths are deleted. `Match` owns `std::unique_ptr<Referee> referee_`;
  `Referee`, `Foul`, `RefereeBuffer`, offside, set pieces and disciplinary state
  remain football rules. There is no substitute official actor or synthetic
  look-at position; stopped players use their existing idle/ball-facing branch.
- Card restarts use the fixed rule budget `kCardRestartDelayMs = 10000` in
  `Referee::CheckFoul`, in addition to 2000 ms preparation and 2000 ms to the
  whistle. The legacy compressed-clock policy still skips 1900 ms plus the
  card budget when `animations == false`; it never reads an official clip.
  `AlterSetPiecePrepareTime` and the Match animation-state timing feedback
  are deleted. Yellow/red issuance and the existing effective-time/send-off
  policy are unchanged.
- Removing officials intentionally removes their profile/reset RNG draws.
  This is a semantic change, not bit-exact compatibility: raw WorldState
  goldens at ticks 1/100 stay identical, 500/1000 change; every simulation
  digest changes because it no longer includes the three official actors.
  The new goldens explicitly document this change. Football-player ordinals
  remain unchanged (officials were allocated after the teams).
- `PlayerBase` is flattened into concrete, final, non-polymorphic `Player`;
  `playerbase.*` and its build entries are deleted, with no alias/shim. `Player`
  directly owns movement/action/execution-queue state and `unique_ptr<Humanoid>`,
  not an AI controller. Human/control/Humanoid readers take `Player*`/`Player&`.
  Player-specific difficulty/fatigue-adjusted `GetStat` and `Process` are the
  only implementations; unused base fallback implementations are gone.
- Flattening preserves the two ordinary `Deactivate` resets and their two RNG
  draws/continuity epochs. Teardown calls only private `ResetRuntimeState` once
  per active player (zero for inactive), exactly the old base-destructor window;
  do not invoke roster callbacks while `Team::Exit` is deleting the roster.
  This change preserves all post-official-removal goldens without regeneration.
- Environment ownership, identity/order/scheduling separation and Player flattening are
  complete. The Humanoid/HumanoidBase hierarchy remains untouched; its flattening
  and the dormant legacy input/controller cluster are separate follow-up work.
  Environment ownership removal preserves the current goldens and RNG windows
  without regeneration.

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
