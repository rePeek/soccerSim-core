# AGENT.md

Guidance for AI agents working in this repository.

## Project and working rules

`soccerSim-core` is a deterministic, headless football simulation in C++23,
originally forked from GameplayFootball / Google Research Football / blunted2.
Remote: `git@github.com:rePeek/soccerSim-core.git`. License: Apache-2.0; the blunted
foundation boilerplate is public domain.

- `main` is the primary development branch, promoted from `feat/anim-base`. Treat
  the checked-out files and this history as the source of truth.
- The former `main` history is preserved on `archive/main-before-anim-base`; it
  contains the abandoned, unrelated architecture refactor and is not developed.
  The ignored `REFACTOR_PLAN.md` describes that archived work, not this branch's roadmap.
- Do not create `src/core/` or `src/legacy/`.
- Structural changes must update this file's layout and target/path references.
- Finish changes with a local git commit. Never push; the maintainer does that.
- Do not casually edit `data/` or the baked asset: they are regression inputs.
- Determinism is the product. Run regression tests after refactors; do not update
  golden values unless an intentional semantic change is explicitly documented.

## Build and test

CMake >= 3.24, Ninja, C++23; use the `flake.nix` dev shell:

```sh
nix develop --command bash -c 'cmake --preset release && cmake --build --preset release -j 4 && ctest --preset release'
# debug is another supported preset
build/release/football_app
build/release/football_app --half-duration-ms=1800
```

BqLog 2.5.0 (pinned commit) is fetched by `cmake/CPM.cmake` in every build mode;
Catch2 v3.7.1 is fetched only with `BUILD_TESTING=ON`. Both use `.cache/CPM`.
CTest registers whole test executables with add_test; Catch2 runs every case and
reports case-level failures. No stdout-based discovery, build-time test execution,
output filtering or third-party patch is needed. To select a Catch2 case, invoke
its executable directly, e.g. build/release/football_sim_control_boundary_test '[failure]'.
Core-only configuration needs network on the first dependency fetch:

```sh
cmake -S . -B build/core -DBUILD_TESTING=OFF -DFOOTBALL_BUILD_APP=OFF
```

The app args/fixture archive remains an explicit `EXCLUDE_FROM_ALL` target even
when CLI/tests are disabled. It is never linked into the shared library.
CTest sets `GFOOTBALL_DATA_DIR=$PWD/data` for regression's offline import fixtures.
Runtime does not use that variable: it reads the configured baked animation asset.

## Source layout

```text
src/
├── foundation/       general math and geometry; STL/self only
│   ├── math/         vectors, matrices, quaternion, scalar and RNG
│   └── geometry/     line and triangle
├── model/            STL-only static football descriptions
│   ├── player.hpp    PlayerId, legacy PlayerDatabaseId, abilities and appearance
│   ├── team.hpp      rosters, typed tactics, TeamSide (Home/Away, not club identity)
│   ├── formation.hpp initial formation + normalized TacticalFormation declarations
│   ├── pitch.hpp     sole pitch geometry type (legacy 110 × 72 metres)
│   └── football_types.hpp roles/game modes/player count
├── sim/              rules, physics, execution and owning value contracts
│   ├── world_state.hpp / observation_epoch.hpp owning output and match identity
│   ├── player_control*.hpp executable control values
│   ├── match_options.hpp / match_phase.hpp / match_result.hpp rule/result values
│   ├── tick.hpp / tick_boundary.hpp strong 100 Hz time values and exact boundary conversions
│   ├── simulation.*  owns Match, deterministic RNG and baked animation library
│   ├── match*, team, ball, referee, formation, gamedefines, rng
│   ├── pitch_frame.* shared runtime ↔ canonical home-pitch adapters (sim-private)
│   ├── animation/    baked schema/library/selector; depends only on foundation
│   ├── query/        player queries and reachability
│   ├── rules/        offside and restart placement
│   ├── ai_support/   mentalimage: execution history, not decision policy
│   └── player/       concrete Player, controls, locomotion, mechanics and scheduling
│       └── humanoid/ Humanoid / HumanoidBase / utilities
├── ai/               value-only decisions; never links sim runtime
│   ├── ai_config.hpp startup values; no live channel
│   ├── tactical_board.* persistent desired/planned values + model-only bootstrap
│   ├── team_requests.hpp short-lived intent bound to observed tick/reset/epoch
│   └── default_ai.*  WorldState + boards/requests → PlayerControlSet
├── app/              executable-side code, never in core
│   ├── app.cpp / args.* parse → GameEnv → complete match → write Result
│   └── fixtures/     typed sample rosters/profiles → model; local six-decimal quantization
├── env.hpp           public autonomous match façade; values + opaque private owners
└── env.cpp           sole product composition implementation for AI + sim

cmake/
├── CPM.cmake
└── bqlog.cmake            private third-party diagnostics build

tools/
├── football_regression.cpp developer regression/animation A/B diagnostic
├── animBaker/              offline importers and callable bake/check/verify library + CLI
│   └── import/legacy_*     offline-only XML, value codecs and required text helpers
├── 4f-b-pure-locomotion-animation-read-audit.md
├── 5a1-contact-authority.md
└── time-model-migration.md  completed tick stages and explicit semantic boundaries

test/                        C++/Catch2 unit and integration tests, no shell guards
├── gameenv_test.cpp          public façade lifecycle and composition equivalence
├── player_identity_test.cpp  identity-independent runtime/RNG replay
├── sim_contracts_test.cpp    standalone input/output values
├── model_test.cpp            standalone STL domain checks
├── app_*_test.cpp            args, fixtures and CLI composition
├── default_ai_test.cpp + default_ai_fixture.hpp
├── sim_computation_test.cpp  queries, reachability, offside, kick mechanics
├── tick_test.cpp             typed arithmetic, overflow and non-grid boundary rejection
├── sim_control_boundary_test.cpp controls/frames/reset/replay
├── sim_match_lifecycle_test.cpp phases/clocks/end changes/result/freeze
├── pitch_frame_test.cpp     half/order geometry, nonzero ball/velocity and control round trips
├── match_tick_test.cpp      timeline authority, restart tail, freeze and invalid boundaries
├── restart_placement_test.cpp + restart_placement_fixture.hpp
├── anim_baking_test.cpp      two independent bakes, byte equality, field/selection verification
├── legacy_animation_import_test.cpp parser semantics, codecs and catchable failures
└── baselines/               historical policy/input/phase schema goldens
```

## Build ownership and architecture

Each module explicitly declares its sources, public `FILE_SET HEADERS`, aliases
and link edges in its own `CMakeLists.txt`. No source globbing or `sources.cmake`.
Root CMake directly builds `game` from `src/env.cpp`, publishes `env.hpp`,
and provides `football::game`. There is no env directory, env target or intermediate
engine object target. Runtime whole-archive packaging preserves existing exported
symbols (including the currently unused GetRoleFromString).

```text
app → game (env.cpp) → ai + sim
ai → ai_contracts + sim_contracts → model/foundation
sim → sim_contracts + animation → model/foundation (PRIVATE BqLog)
app_support (args/fixtures) → model/foundation
anim_baking → legacy_anim + animation/foundation
```

- `football_sim_contracts` is header-only and owns the exact value headers listed
  above (including MatchOptions); sharing the sim directory does not expose actors.
- `football_ai_contracts` owns AIConfig and TacticalBoard declarations; it depends
  on model/foundation, not concrete policy. `football_ai` owns policy implementations.
- Game's PUBLIC usage requirements are model + sim_contracts + ai_contracts.
  Concrete sim/AI are PRIVATE. `env.hpp` never includes default_ai.hpp.
- GameEnv::Start creates/reconfigures the single process logger `football` before
  constructing Simulation/AI; Stop destroys AI then Simulation and flushes it.
  BqLog is PRIVATE to game/sim, never in public value contracts or policy.
  Warning sites tolerate a missing logger for direct Simulation users. BqLog
  uses unpatched upstream sources and keeps its normal Debug assertions/output;
  no forced NDEBUG or BQ_TOOLS/BQ_UNIT_TEST workaround.
  Runtime warnings use BqLog; resource/environment failures throw runtime_error,
  runtime contract failures throw logic_error, and debug-only oracles/invariants
  use <cassert>. No product signal handlers/backtraces or deliberate crash logging.
  Current product scope is one process/one match: no logger registry, batch or
  new general infrastructure/utils module. There is no src/support or support target;
  file I/O uses the STL at its call sites, Properties and custom diagnostics are removed.
- `football_animation` is an independent archive. The offline baker can load and
  verify baked assets without linking Simulation or GameEnv.
- `football_legacy_anim` is an offline object target. `football_anim_baking` is a
  callable offline library; `football_anim_baker` is its CLI.
- `tools/CMakeLists.txt` owns real developer tools. `test/CMakeLists.txt` registers
  C++ tests, regression runs, real app short/full matches and baker CLI checks.
  Tools are currently built under BUILD_TESTING, never linked into core.
- Source ownership and dependency boundaries are maintained directly in each
  module's CMakeLists and reviewed alongside C++ includes. There is no centralized
  architecture validator or separate dependency allow-list.
- All targets use `src` as an include root; keep includes consistent with module boundaries.
- Core must not gain graphics, Boost, app fixtures or offline animation imports.
  model/foundation must not depend on upper layers. sim must never depend on AI.
- model and foundation retain standalone CMake support.

## Test targets

- `football_regression`: independent kinematics, body-facing, locomotion, collider,
  scheduler, command-adapter, raw WorldState and authoritative-state/RNG replay
  checks; model ownership, canonical frames, cached-animation lifetimes and offline
  imports; fouls/cards/restarts, penalty, advantage, offside; Player fatigue/stats
  and teardown RNG windows. Options: `--print-baseline`, `--animation-ab`,
  `--animation-ab-lifecycle`. A/B uses reset/replay, not checkpoints.
- `football_gameenv_test`: lifecycle errors, exactly-one-step stepping, copied
  constructor/config values, owning snapshots, independent live owners, atomic
  failed startup, complete short matches and Stop/Start replay.
- `football_player_identity_test`: sparse/full-width IDs, reordered/reversed/unequal
  rosters, side changes/send-offs and >256 entries; identity-independent numerical,
  policy, scheduling and RNG fingerprints. Supports `--print-baseline`.
- `football_model_test` and `football_sim_contracts_test` link only their value
  targets; `football_default_ai_test` links only policy/contracts/Catch2.
- `football_sim_control_boundary_test` includes lifecycle and restart tests.
  Direct PlayerControlSet tapes cover every WorldState payload field and RNG state
  at all 400 frames, both processing orders, independent replay and Stop/Init replay.
  No policy or external action protocol is involved in this replay test.
  Restart placement covers 72 original-source cases: 6 restart types × 2 processing
  orders × 2 taker sides × 3 roster shapes (11/11, 3/2, 1/1).
  Pitch-frame regressions cover both halves × both processing orders, repeated
  physical end changes, exact player/ball distance preservation, input immutability
  and DefaultAI chasing the real ball instead of its ghost mirror.
- `football_anim_baking_test`: independent source loads/bakes produce identical
  bytes, artifact round-trip/field/selection checks and failure cases. CTest also
  invokes the real baker's --check, --verify, --verify-selection flags.
- `football_legacy_animation_import_test`: original XML recursion, duplicate/tag
  ordering and whitespace semantics; token appending, codecs, body-part/XML/file
  failures throw instead of terminating the process. Links only offline/foundation.
- Fixture tests preserve both sides' abilities bit-for-bit against fingerprints
  captured before replacing XML; six-decimal quantization is fixture-local.
- The actual `football_app` supplies black-box short/full match coverage. There is
  no separate smoke executable and no shell architecture-test tier.

## Autonomous runner and runtime authority

`GameEnv(home, away, pitch, MatchOptions, ai::AIConfig)` copies declarations and
owns one Simulation and one opaque DefaultAI. No default constructor, copy/move,
reset, runtime accessor or live control/tactics/request API.

```text
app main → GameEnv::Start
         → until Finished: Observe → DefaultAI::Update → controls → Simulation::Step
         → Result (application serializes it)
Simulation → Match → Ball / Team / Player / Humanoid / Referee
```

- Start requires stopped state and initializes local owners before publishing
  either. Failure remains stopped. Stop is idempotent and releases both; Start
  recreates the original declarations/config and reuses the named process logger.
  Parsing, formatting and serialization belong to app, not GameEnv.
- One Step means one 10 ms simulation step. After full time it is a no-op including
  AI; stopped Step/Observe throw logic_error. Finished is false while stopped.
  Result throws before full time or after Stop; stopping never invents completion.
  Observe/Result return owning values that can survive teardown.
- `football::sim::Tick` is an absolute timeline instant; `TickSpan` is a duration.
  `sim/tick.hpp` owns the fixed 100 Hz quantum and float seconds derived from it,
  not foundation or runtime configuration. No absolute-time + absolute-time API.
  Boundary conversions live in `tick_boundary.hpp`; non-grid milliseconds are
  rejected, never silently rounded. Existing millisecond runtime APIs remain
  transitional until their individual owners migrate. Keep unit migration separate
  from dead-ball/clock-scale semantics; preserve float arithmetic and numerical goldens.
  Match stores only `Tick now_` for its timeline; normal steps call
  `AdvanceTime(TickSpan{1})`, and WorldState reads it directly. Remaining `_ms`
  timeline accessors are temporary exact adapters, not duplicate state.
  Actions store only elapsed/duration/optional contact `TickSpan`; animation frame
  readers are projections, not duplicate clocks. Decision and locomotion schedulers
  use Tick deadlines and owner-local TickSpan cadences. Their tests are in
  `test/player_action_tick_test.cpp`; old diagnostic digests project milliseconds.
  Player touch/card-effect timestamps are Tick; unused possession-duration storage
  is removed. Humanoid touch-time readers temporarily project milliseconds.
  The legacy scaled football clock remains millisecond-based until its semantic migration:
  it supports sub-tick progress. See tools/time-model-migration.md before removing
  scale, rounding arrival estimates, or replacing the old restart preparation tail.
- Referee owns period transitions; Match owns phase, football clock, score and
  executed-step count. MatchPhase is PreMatch/FirstHalf/SecondHalf/Finished;
  second-half kickoff preparation is inside SecondHalf. Defaults are two 45-minute
  halves, no added/extra time or penalties. CLI has no tick budget/--steps.
- `match_duration` preserves legacy factor `value * 0.2 + 0.05`. Validate finite,
  non-negative, non-stalling scale and positive/non-overflowing half duration before
  RNG draws. Per-step scaled increments truncate, accumulate as uint64 and clip at
  period boundaries. Whistles win over pending restarts and that tick's ball contact.
- Terminal Match freezes clocks, actors, actions, ball, scores, RNG and Result.
  duration_ticks counts executed Steps including the terminal transition; World.tick
  is the simulation timeline including restart skips. Football match_time_ms is
  separately paused/scaled; never conflate these three measures.
- Half time mirrors both teams/ball/mental images once at the next canonical
  between-tick frame. Static physical sides flip, while WorldState keeps the home
  frame and TeamSide remains Home/Away. The same physical goal credits the opposite
  team in the second half. First-half numerics remain unchanged.
- `pitch_frame.*` owns the runtime/home-pitch conversion for both observation and
  control execution. Team transforms derive from current dynamic side versus fixed
  Home=-1/Away=+1; the between-tick ball uses the first processing roster's frame.
  Never infer orientation from MatchPhase: SecondHalf is published before the
  pending physical change of ends. Ball position AND velocity must share the same
  home-pitch frame as player positions; defending_direction does not flip.
  Fixing the missing ball transform intentionally changes second-half AI matches,
  not first-half numerical goldens, clock policy, physics or animation resources.
- WorldState is an owning projection, not authoritative storage or a history cache.
  Its opaque ObservationEpoch owns only a fresh empty lifetime marker; equality,
  no actor data/counter/RNG/clock/address-number ID. Physical hashes exclude epoch.

## Policy and execution boundaries

- DefaultAI owns TacticalBoards and TeamRequests; no actor pointers, callbacks,
  hidden observation caches or RNG. Model-only MakeTacticalBoard initializes base
  roles/anchors/spacing. Update reads values and emits frame-local controls.
  AIConfig may copy initial boards by side; no mutable GameEnv board exists.
- TacticalBoard is desired/planned intent, never restart/score/current state.
  Width/depth are pitch fractions (defaults 0.75/0.55); anchors are home-frame metres.
  Update computes transient targets without rewriting boards.
- Requests bind player IDs to issuance tick/reset_sequence/epoch. New Matches or
  rule resets cannot revive old intent; explicit ResetRequests is cleanup, not a
  correctness precondition. Unbound synthetic observations cannot issue requests.
- RefereeBuffer is sole restart type/taker/prepare/start authority. sim/rules owns
  restart placement/taker/retain mechanics. No duplicate AI restart authority.
  Stop/prepare/start/foul are `Tick`; relaxation is `TickSpan`. Due checks accept
  crossed deadlines and prepare/whistle are one-shot via taker/play state.
  Match owns preparation fast-forwarding, retaining the old ten-tick simulated
  tail during unit migration. This is temporary scheduling policy, not football
  readiness; later replace it with Pending/Ready/Taken and minimum/timeout bounds.
- Diagnostics/tests may compose Simulation and explicit PlayerControlSet sequences
  outside GameEnv. Equal declarations + equal control tapes must replay identical
  WorldState payloads and RNG states; epoch identities intentionally differ.
- The GRF action protocol, player selection/sticky state, TeamDecisionRequest and
  app input target are removed. The app only configures and runs an autonomous match.
- DefaultAI::RequestAttackingRun/RequestTeamPressure/RequestKeeperRush and their
  transient intent/lifecycle tests remain unchanged for a separate ownership audit.
  They are not app-facing input APIs and are never exposed through GameEnv. Decide
  whether they become AI-internal intent or a future coach mechanism separately;
  do not retain them merely for a retired compatibility client.
- Simulation has no decisions/input ownership. Empty controls mean idle movement;
  control execution translates frames, resolves active model IDs and rejects illegal
  hands saves/inactive recipients. Mechanics remain in sim, not policy.
- Default policy replacement was intentionally semantic, not bit-exact Eliza
  migration. Historical goldens live in test/baselines/pre_value_ai.md. Do not
  reconstruct actor-calling decision ports or insert dummy RNG draws to match them.

## Identity, ownership and numerical invariants

- PlayerId is caller-provided uint32 identity in model/player.hpp. PlayerDatabaseId
  is import provenance only. No ID remapping/allocator, PlayerIndex, generic IDs
  module, TeamId or ambient actor numbering. Roster positions are local indices.
- Player holds a reference to the model::Player owned by Team; Team copies the
  model::Team and derives runtime FormationEntries. Match owns score/possession.
  Sample factories live in app/fixtures, never model or core.
- Reject invalid/duplicate IDs, empty rosters, formation/profile mismatch and invalid
  time rules before consuming RNG. Init seeds to 0, draws one Uniform(1,4) per
  declared profile (even explicit appearance and omitted formation profiles), then
  applies the configured seed and builds actors. Preserve this reseed window.
- Simulation owns a bit-identical std::mt19937-based RNG; Match holds a reference.
  Never add hidden globals, wall clocks or unordered iteration to simulation logic.
- Player is concrete, final and non-polymorphic. Its private uint8 schedule_phase_
  is a non-unique 0..9 stagger, not identity and never exported. The second roster
  starts at first-roster size modulo 10 even with reversed processing. Send-offs
  do not compact phases. Preserve the 100 ms scheduling cadence.
- Two normal Deactivate resets preserve two RNG draws/epochs. Teardown invokes
  only private ResetRuntimeState once per active player, zero for inactive; no
  callbacks into a roster being destroyed. Player::Mirror skips never-active bench
  entries with no Humanoid but keeps active/sent-off mirror behavior.
- Humanoid/HumanoidBase flattening is separate work. Animation cache, 10 ms step,
  private phases, destruction order and appearance/reseed windows stay unchanged.
- Officials are rules, not animated actors. Referee is unique_ptr-owned by Match;
  no PlayerOfficial, official profiles or animation-driven restart timing remains.
  Legacy card budget is 1000 ticks + 200 preparation + 200 whistle ticks.
  Referee schedules deadlines; Match fast-forwards to preparation minus its
  existing ten-tick simulated tail (40 skipped ticks after a goal, 190 for ordinary
  restarts, plus the card budget). Initial/half-time kickoff deadlines remain
  unchanged. There is no animation/waiting-mode switch; baked animation resources
  remain mandatory for player motion/actions/contact.
  Official removal intentionally changed RNG/goldens; current baselines include it.
- No ambient environment/context/RNG, ScenarioConfig, controller hierarchy, retired
  GRF binding or memory-hook checkpoints. Durable saves would need explicit values.
- Pitch is model::Pitch only (legacy geometry currently); formation runtime role
  adaptation is sim algorithm code, no XML/DB lookup.

## Animation and ongoing work

Runtime reads only `assets/runtime/animations.simanim`, via AnimationLibrary and
BakedAnimationSelector. Simulation loads/shares the library with Match/Humanoid;
there is no ambient animation owner. Offline `.anim`/XML/object parsing lives under
`tools/animBaker/`, never libgame.so. Two include roots are distinct:
`sim/animation/...` is runtime, `animation/...` is offline tooling. Their filenames
are disjoint. The original XML parser, codecs and necessary token/decimal helpers
are owned by `tools/animBaker/import/legacy_*`, compiled only into the offline
`football_legacy_anim` target. Keep parser semantics, including multimap ordering,
duplicate tags and whitespace removal; do not replace it with a new XML library.

Active movement work is animation root motion → explicit procedural kinematics:
player_kinematics, player_locomotion, player_body_facing and decision/locomotion
schedulers. Before changing movement/contact authority, read both audit notes under
tools/. Pure locomotion means procedural Movement without scheduled contact or
retained ball; smuggle offsets are transitional keyframe corrections; Shot/Pass/
Trap/BallControl still animate their contact frame. Measurement-only changes must
keep regression output exact.

## Conventions

C++23, extensions off, PIC on. Namespaces: football::model, football::ai,
football::app, blunted. Legacy sim/value classes remain global. Never duplicate
an authoritative field as a cache without need. Preserve Mirror/reverse processing
symmetry. Model must not include simulation or orchestration headers.
