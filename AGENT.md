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
- **Tests are scoped to the module being developed.** Routine module work runs
  module unit tests; full matches run only in explicit Runtime integration
  acceptance. See the Module testing policy in "Build and test" below.

## Build and test

CMake >= 3.24, Ninja, C++23; use the `flake.nix` dev shell.
Development validation requires **Debug and core only**. Build Release only for
the explicitly selected Level 3 Runtime acceptance run (see the Module testing
policy below).

```sh
# Core build + the (currently empty) test scaffolding
nix develop --command bash -c 'cmake --preset debug && cmake --build --preset debug -j $(nproc) && ctest --preset debug'
# Core-only build
nix develop --command bash -c 'cmake -S . -B build/core -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Debug && cmake --build build/core -j $(nproc)'
```

BqLog 2.5.0 (pinned commit) is fetched by `cmake/CPM.cmake` in every build mode;
nlohmann/json v3.11.3 (pinned, `cmake/nlohmann_json.cmake`) is the coach
tool-call JSON boundary, also fetched in every build mode; Catch2 v3.7.1 is
fetched only with `BUILD_TESTING=ON`. All use `.cache/CPM`.
`flake.nix` provides ccache and the root CMake sets `CMAKE_{C,CXX}_COMPILER_LAUNCHER`
to it whenever it is found, so local rebuilds are cached automatically; a missing
ccache is a silent no-op. `-j $(nproc)` uses all host cores.
### Module testing policy

Tests are scoped to the module being developed. Each layer proves a different
thing, and its cost is paid only when it is relevant.

- **Level 1 — module unit tests** (`unittests/<module>/`, label `unit`). The
  module proves its own physics, geometry, rules, actions, data and ownership
  in isolation, without a running match. Run these for routine development and
  refactoring: `ctest --preset debug -L unit`.
- **Level 2 — interface and bounded integration** (the adapter tests, the
  public-header build smoke and `unittests/runtime/`, label `interface` or
  `runtime`). Independent linking, object lifetime, data passing and a small
  number of ticks through the real composition. Run these when a public
  interface or a cross-module boundary changes, not for every module edit:
  `ctest --preset debug -L interface`.
- **Level 3 — Runtime full-match acceptance** (`unittests/integration/`, label
  `integration`). Opening/period/full-time lifecycle, score, events and match
  completeness. Release-only and opt-in: `ctest --preset release -R autonomous`
  (or `-DFOOTBALL_INTEGRATION_TESTS=ON`). Never part of the default CTest suite.

Every module proves its own logic; Runtime proves the modules compose into a
complete match. `event_unit` covers the formal event log and its cursor, and
`referee_unit` links the independent `football_referee` library (foul,
advantage, offside and release rules over value facts). The event recognizer
leaf is the remaining Level 1 gap.

Rules:

- A module is complete when its own unit tests and the interface contracts it
  touches pass. Full-match acceptance is not a prerequisite for merging an
  independent module change.
- A module change must not require a full-match simulation.
- Full-match simulation is reserved for explicit Runtime integration and
  acceptance phases, not a per-change smoke test.
- Long-running tests must be explicitly selected, bounded by a CTest timeout
  and kept out of routine module validation.
- Preserve meaningful deterministic regression tests as optional acceptance
  tests. The 22-actor / 220-tick determinism case (A17) is tagged
  `[determinism]` and registered only with `-DFOOTBALL_DETERMINISM_TESTS=ON`; it
  is an Animation/Player + Runtime integration acceptance, not a unit test.

The default `ctest --preset debug` runs Level 1 and Level 2 only. A full-
activity Step costs O(100 ms) on the reference host, which is why the Level 3
half is deliberately short and opt-in. The former golden/end-to-end tier and
`unittests/baselines/` are gone for good.

Core-only configuration needs network on the first dependency fetch:

```sh
cmake -S . -B build/core -DBUILD_TESTING=OFF
```

The app args/fixture archive remains an explicit `EXCLUDE_FROM_ALL` target even
when CLI/tests are disabled. It is never linked into the shared library.
Runtime reads the configured baked animation asset, never a data-dir env var.

### Autonomous-match integration acceptance (Release-only)

`unittests/integration/autonomous_match_test.cpp` is the only place a full
autonomous match is allowed. It links only `football::simcore` (the product
`GameEnv` composition) and runs to `Finished()` in bounded short halves. It is
built only when `FOOTBALL_INTEGRATION_TESTS` is on (Release preset default;
`-DFOOTBALL_INTEGRATION_TESTS=ON` elsewhere) so the Debug CTest budget is
untouched. Run it with `ctest --preset release -R autonomous`, or:

```sh
FOOTBALL_INTEGRATION_HALF_SECONDS=2700 ctest --preset release -R autonomous
```

It covers Default x Default completion, Heuristic x Heuristic completion, a
Team set-piece-orders/preset match, and same-seed determinism (per-tick world
digest, the full confirmed `MatchEvent` sequence read incrementally through
`GameEnv::ReadEvents`, the event-trajectory sequence and the final result).
`Finished()` alone is not acceptance: the audit also asserts restarts exist
and are actually taken, no long restart stall or sustained no-action,
finite/in-bounds positions, possession exists, two `PeriodEndedEvent`s, goal
events equal the final score, and `RestartTakenEvent` count <= awarded count.
The heuristic run also guards the concrete blocker found here: `AIEngine` must
construct every selectable `PlayerPolicyKind` (the V2-only Heuristic kind must
not require a legacy adapter).

### Pitch owns the static physics scene (its own archive)

`football::pitch` is a STATIC archive owning `football::pitch::Pitch`
(`include/football/pitch/pitch.hpp`, implemented in `src/pitch/pitch.cpp`).
Pitch builds and caches the whole static scene once: the ground plane, the four
goal posts, the two crossbars, the ground/goal parameters and the fixed
environment (gravity, wind). No goal-net damping is simulated. It depends on
foundation + geometry and never on ball/player. The header keeps the
description, the constexpr dimensions and the three trivial accessors
(`gravity`, `wind_velocity`, `colliders`) inline; the configuration validation,
`operator==` and the collider generation are `pitch.cpp` implementation detail.
`pitch_frame.hpp` stays header-only: it is pure, stateless geometry.

Dependency direction: `football::pitch` -> `football::geometry` ->
`football::foundation`; `football::ball` and `football::player` read
the immutable pitch scene. `football_pitch` must never link `football_ball`.
No C++20 module, no module toolchain, no extra CMake flags: any supported
compiler builds it, and like every other module it declares its own sources,
headers and link edges in `src/pitch/CMakeLists.txt`.
`simulation.hpp` transitively includes `player_active_touch_shadow.hpp` (which
pulls `player_active_touch_model.hpp` and `football/ball/ball.hpp`) and
`player_baked_pose_sample.hpp` (which pulls the baked body-pose/contact-region
geometry). Editing any of those inline headers recompiles 13 translation units
(the runtime domain's `simulation*.cpp`, `player_touch_execution.cpp` and
`src/env.cpp`, plus the runtime suites) and relinks 11 executables: a measured 24
ninja edges in ~8 s at `-j 16` with ccache disabled. The deferred fix is to move
value-typed state (`PlayerBakedPoseSample`, `ActiveTouchShadowReport`,
`ActiveImpulseCandidate`) out of the `Simulation` class definition behind a
pimpl or a private state holder so those headers stop appearing in
`simulation.hpp`; do not attempt this as an incidental change mid-migration.

## Source layout

```text
include/football/    public headers (interfaces), one directory per module
├── foundation/      the low-level layer. Directory organization only; each
│   │                subdirectory is its own CMake target.
│   ├── math/         Vector3, quaternion, scalar, RNG (football::foundation)
│   ├── time/         Tick / TickSpan (football::foundation)
│   ├── geometry/     Plane/HalfSpace/Sphere/Capsule, ColliderShape,
│   │                 ContactMaterial, Collider and per-tick ColliderMotion
│   │                 (football::geometry)
│   └── collision/    generic Overlap/Sweep/FirstHit CCD, MovingSphere and
│                     CollisionHit (football::collision); Sweep validates its
│                     input (dt > 0, finite/positive radii, matching
│                     start/end kinds) and throws on a violation
├── animation/       header-only baked schema/library/selector (football::animation)
├── ball/            football::ball: Ball, BallModel, BallState, contact
│                    evidence, impulse and per-tick integration; reads the
│                    immutable football::pitch scene and queries via
│                    football::collision
├── pitch/           football::pitch: Pitch description plus the ground/posts/
│                    crossbars colliders and the fixed environment
├── team/            football::team: static TeamDefinition (identity, personnel,
│                    Lineup, Leadership, SetPieceOrders, formation templates/
│                    presets) and the Player-owning Team aggregate. Team depends
│                    on no Runtime/Referee/AI; Runtime composes it. Public headers
│                    under team/.
├── ai/              AIConfig, TacticalBoard (legacy + V2 plan), the pure
│                    TacticalInterpreter/EffectiveTactics, typed coach
│                    commands/context/policy, shout values and DefaultAI
├── model/           football::model: provider-neutral ModelMessage/ToolCall/
│                    Response protocol and the IModelProvider/ModelRequest seam
│                    (STL only; no football, JSON or transport type)
└── env.hpp          public autonomous match façade; values + opaque private owners

src/                 implementations; public headers live in include/football/
├── foundation/      CMakeLists.txt declares football::foundation (math/*.cpp)
│                    and football::geometry (header-only); collision/ has its
│                    own CMakeLists for football::collision
├── ball/            football::ball implementation: motion, generic collision
│                    response and prediction; no goal-net physics
├── pitch/           football::pitch implementation: pitch.cpp validates the
│                    configuration and generates the ground/posts/crossbars
│                    colliders once. The archive links foundation/geometry and
│                    never ball/player.
├── team/            football::team implementation: team.cpp validation/selection,
│                    formation_preset.cpp anchor resolution and team_players.cpp,
│                    which owns the team's Player objects. The archive links
│                    foundation/pitch plus football::player (Team -> Player).
├── ai/              default_ai.cpp, tactical_board.cpp, tactical_plan.cpp,
│                    tactical_interpreter.cpp, eliza/, player/, coach/
├── model/           CMakeLists.txt declares football::model (header-only
│                    protocol/provider) and football::model_mock (scripted test
│                    provider); the concrete HTTP/local providers are separate
│                    compiled into libsimcore.so with everything else under src/
    ├── simulation.*  owns clock/phase/score, ball, two Team aggregates, referee, touch state, RNG and history
    ├── simulation_config.hpp match-level options value

    ├── event/        EventRecognizer/EventTransition, EventLog/MatchEvent, TouchState,
    │                 AcceptedTouch + AcceptedTouchSink and whole-step EventTrajectory
    ├── observation/ owning WorldState, world_state_builder (which applies the
    │                 pitch/pitch_frame.hpp transform), MentalImage/player-image
    │                 history + nearest-slot sampling,
    │                 value-only Snapshot + preallocated SnapshotHistory + temporal_graph,
    │                 static PlayerSlotTable identity -> TeamSide mapping and a pure
    │                 shadow-only Snapshot touch inference
    ├── referee/      Referee state, facts/views/commands, goal, period, offside, ball-state
    │                 rules and restarts
    ├── runtime/      MatchClock/MatchPhase/MatchResult, ActiveLineupState/
    │                 TeamFrame/TeamMatchState (live match state), TeamView/
    │                 team_facts, possession and team_possession, match_config.hpp
    │                 (TeamSetup + TeamSide), formation.cpp BuildFormation,
    │                 team_setup_adapter (transitional TeamSetup -> team::TeamDefinition).
    ├── testing/      internal SimulationAccess, not exported/product or actor-facing
    └── player/       Player + PlayerDefinition, player_role, FormationEntry, tick-local
        │             inputs, controls, locomotion, contacts, roster_query
        │             (span of borrowed players), reachability and perception,
        │             plus the P1 motion layer: player_motion_profile.hpp (ability ->
        │             capability, no fatigue baked in) and player_motion.{hpp,cpp}
        │             (MotionSample + PlayerMotion::SampleContinuous/SampleClip).
        │             Player ground/action/body volumes keep their *decision* logic
        │             (action windows, response, fouls) but take their geometry from
        │             football::collision (2D circle/capsule penetration and the strict
        │             sphere/capsule twins), so no distance intersection is re-derived
        │             here.
        │             Owns ONLY single-player state and behaviour: it never holds a
        │             Team, never iterates the squad and contains no team algorithm.

cmake/
├── CPM.cmake
└── bqlog.cmake            private third-party diagnostics build

tools/
├── app/                    football_sim CLI: parse → GameEnv → complete match → Result
│   ├── args.* fixtures/     executable-side args and sample rosters/profiles
│   └── recording/           SnapshotArchive writer/reader (football_recording)
├── animBaker/              offline importers and callable bake/check/verify library + CLI
│   └── import/legacy_*     offline-only XML, value codecs and required text helpers
└── playerAssets/           standalone Blender player-template/GLB converter and validators

unittests/                        small unit suites, one CTest entry each
├── foundation/     Vector3, Quaternion, scalar, Tick, Rng math contract
├── collision/      swept-sphere CCD (Sweep/FirstHit) and static Overlap
└── ball/           BallModel, response (impulse) and one-tick motion

## Build ownership and architecture

Each module explicitly declares its sources, public `FILE_SET HEADERS`, aliases
and link edges in its own `CMakeLists.txt`. No source globbing or `sources.cmake`.
Root CMake directly builds `simcore` from `src/env.cpp`, publishes
`include/football/env.hpp`,
and provides `football::simcore`. There is no env directory, env target or intermediate
engine object target. Runtime whole-archive packaging retains unreferenced runtime
symbols (including the currently unused GetRoleFromString).

```text
app → simcore (env.cpp) → ai + sim
foundation ← geometry ← collision ← ball ← sim (and later player)
pitch → foundation/geometry (own STATIC archive; ball/player read it)
team → foundation/pitch (static domain; no Runtime/Referee/AI/Player impl)
ai → ai_contracts + sim_contracts → foundation/pitch/collision
ai → model (header-only protocol/provider seam; STL only)
sim → sim_contracts + team + animation + ball + pitch → foundation/geometry (PRIVATE BqLog)
app_support (args/fixtures) → sim_contracts/foundation
anim_baking → legacy_anim + animation/foundation
```

- `football_sim_contracts` is a header-only build/export target, not a directory
  or a contract domain. It links foundation/pitch/team and explicitly exports
  PlayerControl/PlayerControlSet, WorldState, MatchOptions/MatchPhase/MatchResult
  and the two time headers.
  entity and its Player-owning definitions (team_players.cpp). It links
  football::player; sim_contracts re-exports the target.
  Public values live with their domain; directory placement never exports actors.
  The former gamedefines umbrella is split among player, ball, team, match and
  observation. No compatibility headers or common/types layer remains.
- `football_ai_contracts` owns AIConfig and TacticalBoard declarations; it depends
  on foundation/pitch, not concrete policy. `football_ai` owns policy implementations.
- `football::model` is its own leaf module: the conversation protocol, the
  provider seam and the request/response envelopes, with no football, JSON or
  transport type. `football_ai` depends on it PRIVATE, so the AI module calls a
  model without linking libcurl or an inference runtime; HTTP and local
  providers are separate implementation targets (AGENT-3B).
- Game's PUBLIC usage requirements are model + sim_contracts + ai_contracts.
  Concrete sim/AI are PRIVATE. `env.hpp` never includes default_ai.hpp.
- GameEnv::Start creates/reconfigures the single process logger `football` before
  constructing Simulation/AI; Stop destroys AI then Simulation and flushes it.
  BqLog is PRIVATE to simcore/sim, never in public value contracts or policy.
  Warning sites tolerate a missing logger for direct Simulation users. BqLog
  uses unpatched upstream sources and keeps its normal Debug assertions/output;
  no forced NDEBUG or BQ_TOOLS/BQ_UNIT_TEST workaround.
  Runtime warnings use BqLog; resource/environment failures throw runtime_error,
  runtime contract failures throw logic_error, and debug-only oracles/invariants
  use <cassert>. No product signal handlers/backtraces or deliberate crash logging.
  Current product scope is one process/one match: no logger registry, batch or
  new general infrastructure/utils module. There is no src/support or support target;
  file I/O uses the STL at its call sites, Properties and custom diagnostics are removed.
- `football_foundation` holds only math + time. `football_geometry` (header-only
  INTERFACE) and `football_collision` (STATIC) live under src/foundation/ too but
  are independent targets; the directory is organization, the targets are the
  boundary. football_foundation never links them back (that would be a cycle).
- `football_geometry` owns the shared shape/collider contract (Plane/HalfSpace/
  Sphere/Capsule, ColliderShape, ColliderId, ContactMaterial, Collider,
  ColliderMotion). Plane is a two-sided thin surface; HalfSpace is a solid whose
  interior is on the -normal side (the ground). ball, pitch and the player body
  all link it, so none of them owns geometry and the player no longer reaches
  into `football::ball` for a Capsule. ContactMaterial stays here deliberately
  (producers attach it to the collider they emit; collision never reads it) --
  there is no separate material/physics module yet.
  no contact returns nullopt.
  It also owns the planar penetration tests players use for horizontal
  volumes (`PenetratesCircleCircle2D`, `PenetratesCapsuleCircle2D`) and the
  strict sphere/capsule twins of Overlap (`Penetrates`). Strict means exact
  tangency is not a contact: the legacy player contest, tackle and body tests
  used `<`, and switching them to Overlap's `<=` would change which
  shoulder-to-shoulder and edge-of-reach duels register. Player keeps only the
  *decision* around these queries (action windows, response, fouls); it must not
  hand-write its own equivalent distance intersection.
- `football_animation` is now two STATIC archives (ANIM-2 A12), both declared in
  `src/animation/CMakeLists.txt`: `football::animation_base` (the `.simanim`
  format, data structures, reader and base `CrudeSelection`) and
  `football::animation` (Match/MatchTouch, movement/touch refinement, pose
  sampling/blending, playback and transition). The offline baker links only
  `football::animation_base`, so it still links no runtime archive; Player links
  `football::animation`.
- `football_legacy_anim` is an offline object target. `football_anim_baking` is a
  callable offline library; `football_anim_baker` is its CLI. The baker is
  self-contained: it links only its own offline objects and foundation, never
  sim/ball/pitch/simcore/animation archives.
- `tools/animBaker/` owns the offline baker library + CLI. The baker is built
  under BUILD_TESTING and never linked into core.
- `tools/playerAssets/` is standalone Python/Blender tooling, outside CMake and
  the runtime. It reads immutable `data/`, reconstructs legacy color-encoded
  weights/base-to-straight binding and writes ignored `build/player-assets/`.
  Run its stdlib tests and Blender export/GLB verification as documented in
  `tools/playerAssets/README.md`. This is an appearance/template preview, not
  a change to simulation poses, baked assets or a finished modular mod API.
- Source ownership and dependency boundaries are maintained directly in each
  module's CMakeLists and reviewed alongside C++ includes. There is no centralized
  architecture validator or separate dependency allow-list.
- Public headers are consumed from the top-level `include/` root as
  `football/<module>/...`; `src` remains the include root for the not-yet-migrated
  `sim/` headers and for module-private implementation headers.
- Core must not gain graphics, Boost, app fixtures or offline animation imports.
  foundation must not depend on upper layers. sim must never depend on AI.
- ownership boundary (TEAM module): `football::team::TeamDefinition` is the
  copyable static football-domain value (name, PlayerDefinitions, Lineup,
  Leadership, SetPieceOrders, formation templates/presets, bootstrap tactics).
  `football::team::Team` is the non-copyable/non-movable aggregate that owns the
  actual Player objects: its `owned_` container is the sole owner and `Players()`
  is a stable non-owning span in declaration order. Neither depends on
  Runtime/Referee/AI. `Simulation` composes two `unique_ptr<Team>` with
  Runtime-owned `active_lineups_` (live slot bindings), `team_frames_` and
  `team_states_`. Runtime also owns Ball, Referee, touch/possession state and RNG;
  TeamFrame/MatchState/ActiveLineup are not Team members.
- `Player` owns its immutable `PlayerDefinition` by value, body profile,
  identity, formation, kinematics and behaviour. It never holds `Team*` and
  `src/player/` never includes team headers or iterates a squad. `Team` is the
  only Player owner; no second ownership container exists. Team-level algorithms
  live in `src/runtime/` (possession, team_possession, formation, team_facts).
  Possession identifies a team by `TeamSide`, never by a `Team*`;
  `PlayerTeamView` carries borrowed player slices and live `TeamMatchState`.
- `TeamSetup` remains a transitional external declaration in
  `football/runtime/match_config.hpp`. `runtime/team_setup_adapter` converts it
  once to `team::TeamDefinition`; GameEnv/AI still accept TeamSetup until their
  callers migrate. Never introduce a second writable Team/definition store.
  Player copies definitions, so no external definition address is borrowed.
  integration is added. `team/formation_preset` templates are reusable data,
  never the live restart plan or player-control output.
- Restart taker priority and presets are Runtime planning data: `Simulation`
  builds a per-instant `RestartPlanningTable` from each `Team`'s
  `Definition()` and the live `active_lineups_`, and `PlanRestart` consumes it.
  The declared taker wins when active; empty or ineligible orders keep the
  legacy nearest-active-player default. The heuristic AI only consumes the
  resulting `restart_target`, so no AI code changes.
- Init failures and Stop share ClearComposition: stop production adapters,
  Exit actors in home/away declaration order, clear actor borrows/indices,
  release Referee/adapters, clear Runtime team state, then destroy the Team
  owners (which destroy their Players) and clock. The same route handles
  partially constructed Teams and an unpublished Ball. Repeated Stop is safe.

### Motion state writers

`PlayerKinematicState` has one *declared* authority — `Player::CommitKinematicState`,
which also keeps `groundCollider` in step — and nothing may be added here
without stopping to review that rule. The former `Humanoid` mirror and its
reverse sync are gone with the type (commit `60c7378`), so one fact no longer
has two copies. `Player::CommitLocomotion` (committed locomotion) and
`Player::CommitLegacyActionMotion` (the motion the animation/action path
derives) both install through that single commit point, and
`CheckSimulationKinematicOracle()` asserts the invariants wherever a commit is
followed by a completed sync rather than at the commit itself.

`Player::ApplyPositionCorrection(offset)` is the shared commit point for contact
and boundary corrections — the corrected position is committed first and the
animation bookkeeping follows — so collision and reset authority live in the
Player, not in a second object.

Known follow-up (not part of P2c-1): `Player::UpdatePossessionStats()` builds its
interception-prediction `PlayerLocomotionParameters` with only `maxSpeed`
overridden, while execution has used the capability-driven acceleration,
braking and turn rate since P2b. Prediction can therefore disagree with the
simulation about where a player can reach; prediction and execution must
eventually share one effective capability profile.
- Modules are configured from the repository root: public headers live in the
  top-level include/football/ tree, so module directories are no longer
  standalone-configurable. The former src/model module is gone; its data lives
  with its domain.
- `GFOOTBALL_BAKED_ANIM_PATH` belongs only to football_sim PRIVATE compile definitions.
  The shared target helper owns include roots/C++23/PIC/output, not resource lookup.
  No public contract, foundation/model, AI, app or baker inherits the runtime path.

## Test targets

`ctest -L unit` runs the leaf suites; `ctest -L runtime` runs the bounded runtime
harness. There is still no golden/end-to-end tier.

- `football_sim_runtime_tests` (label `runtime`, name `sim_runtime`) is the one
  non-leaf test: it links `football::sim` (never simcore or AI) and builds a real
  `Simulation` to check composition and lifetime only -- two Teams own Players
  with copied definitions, TeamView borrows the Team list, live match state
  stays separate, a few ticks
  `Mirror` twice restores actors/sides/ball, `SwitchEnds` flips both directions
  without touching the between-tick frame, and `Stop` is clean and idempotent.
  It never plays a match and asserts no physics. Baselines (Debug): Init ~0.23 s,
  first Step ~0.77 s, later Steps ~0, Stop ~0.01 s. The CTest `TIMEOUT` is the
  real guard, because bounding the tick count cannot catch a call that never
  returns.
- `football_team_unit_tests` (`ctest -L unit`, name `team_unit`) links only
  `football::team` (foundation/pitch, never Runtime/AI/players): static
  TeamDefinition validation, Lineup/Leadership/SetPieceOrders invariants,
  deterministic eligible selection, reusable formation presets and anchor
  conversion. The Team entity, the TeamSetup adapter and ActiveLineupState are
  covered by the bounded runtime harness.
- `football_foundation_unit_tests` (`ctest -L unit`, name `foundation_unit`) links
  only `football::foundation`: Vector3 length/dot/cross/normalization/rotation,
  quaternion identity/composition/inverse/slerp, scalar clamp/wrap, Tick 100 Hz
  exactness and overflow/negative-interval rejection, Rng determinism/range.
- `football_animation_unit_tests` (name `animation_unit`) links only
  `football::animation`: `ExtractMotionClip` reproduces a clip's baked root
  trajectory frame by frame for a running and a shooting reference clip, drops
  the pelvis bob (z stays with the animation pose), leaves the source clip
  untouched, and handles empty/single-frame/stationary clips.
- `football_collision_unit_tests` (name `collision_unit`) links only
  `football::collision` (geometry + foundation): swept-sphere CCD against
  static/moving/deforming Plane/Sphere/Capsule and the one-sided HalfSpace,
  TOI/normal/center, no-tunnelling, tangent rejection, initial penetration,
  FirstHit earliest/lowest-id tie-break, the static Overlap queries, and the
  malformed-input rejection contract (bad dt/radius/mismatched kinds throw).
- `football_pitch_unit_tests` (name `pitch_unit`) links only `football::pitch`:
  the seven ground/goal colliders, unique ids and fixed order, post/crossbar
  placement, custom dimensions, invalid-configuration rejection and
  configuration-only equality. No Ball, no Simulation.
- `football_ball_unit_tests` (name `ball_unit`) links only `football::ball`
  (+ geometry/collision/pitch it declares), never simcore/AI/animation assets:
  BallModel validation, impulse/momentum and restitution/friction response, and
  one-tick free fall/drag/spin/grass/landing plus the Step/Predict/Reset/Mirror
  lifecycle and illegal-input rejection.
- Keep each test on the smallest target and use analytic expectations (conserved
  quantities, invariants), not values copied from the implementation.

## Autonomous runner and runtime authority

`GameEnv(home, away, pitch, MatchOptions, ai::AIConfig)` copies declarations and
owns one Simulation and one opaque DefaultAI. No default constructor, copy/move,
reset, runtime accessor or live control/tactics/request API.

```text
app main → GameEnv::Start
         → until Finished: Observe → DefaultAI::Update → controls → Simulation::Step
         → Result (application serializes it)
Simulation::Step → explicit domain phases → Simulation-owned competition/actor state
```

- Simulation owns tick sequencing, pending end-change application, execution accounting
  and control dispatch. ball/ball_player_contact resolves passive body contacts from
  explicit players/teams, touch facts, history and Tick values; it does not accept
  Match/Simulation or draw RNG. Per-volume body contacts and intentional touches
  publish one AcceptedTouch through event/accepted_touch_sink.hpp's write-only
  AcceptedTouchSink. The Simulation-owned TouchEvents sink writes Player touch
  tick/type and TouchState team/type/full-width PlayerId records, records the
  diagnostic AcceptedTouch log and immediately dispatches to the event recognizer
  and referee in the same call. MatchTouchSink and Match::SetLastTouchTeamID are
  deleted. Team's touch pointer, NoteLastTouchPlayer and touch queries are also
  deleted. event/touch_query resolves identities from explicit rosters (including
  inactive/sent-off actors), with explicit evaluation ticks for decay. Contact
  sweeps borrow the live TouchState so earlier publications remain visible;
  snapshots or fixed pre-sweep biases would change behavior.
  Publication stays synchronous inside the tick. Never freeze touch
  biases before the sweep: later actors must see earlier touches in the same tick.
- Ball holds a const reference to the immutable football::pitch::Pitch it was
  built with (the caller outlives it), never Match/Simulation, Team, Player or RNG.
  Gravity, ground resistance and the static colliders all
  come from that Pitch; there is no BallWorld/BallEnvironment copy. Rule facts
  (goal, out of play) still arrive as explicit values. Ball::Step is physics only.
  ball/ball_touch_application.*::ApplyBallTouch composes Touch → newest history refresh →
  first/second processing-roster possession refresh from explicit inputs; callers then set
  rotation. Match::TouchBall is removed; Simulation::TouchBall is a diagnostic escape.
  Keep both original prediction recalculations and sample-zero publication timing unchanged.
- Match::Step/Process/StepRemainingTick are removed. Simulation::Step now spells out
  the legacy phase order and all frame/terminal/ceremonial boundaries directly.
  Simulation owns competition state, actors and touch values; the former Match
  value object is deleted, Player/Team upward dependencies are removed, and
  diagnostics use SimulationAccess instead.
- Contact extraction retains cooldown's strict >15-tick boundary and the original
  three RNG argument-expression draws after touch-dependent refresh, before cooldown
  publication. Tests cover standalone Ball/netting, contact feedback/controlled gates
  and explicit time/history sampling. Before/after the removed regression baseline harness and
  restart diagnostics for seeds 42/43/44 × both orders × default/symmetric fixtures
  (30000 ticks per half) are byte-identical. No golden, physics or policy change.
- player/player_contact owns the former humanoid-pair collision algorithm and private
  bounce accumulators. Inputs are Tick, ordered active-player span, const Ball and the
  designated possession player, never Match/Simulation. Simulation supplies first then
  second roster in the common contact frame after possession, before clock advancement.
  Pair offsets remain immediate; movement sharing is accumulated/applied afterward.
  player/player_contact freezes one FoulAssessment per suspicious collision (ids,
  teams, positions, action geometry, last-touch tick, possession) and reports it to
  Simulation: TripMe then the assessment, before any later pair/offset. Physical
  falls and foul verdicts are separate. Referee::AssessFoul evaluates that evidence;
  TripNotice survives only as a direct-test adapter that builds the same assessment
  from live actors and has no implicit-clock wrapper. Preserve strict >60 touch
  grace, standing 2D/sliding 3D radii, double literals, severity arithmetic, immediate
  publication and duplicate-tackler gates. CheckFoul(now, stadium_to_home, commands)
  takes the evaluation instant/frame explicitly, using them for strict recheck/expiry,
  card deadlines and restart scheduling. ScheduleRestart takes tick, setpiece Team*
  and PitchFrameTransform; MakeRestartSchedule and all Referee Match reads are gone.
  The referee instant is split into Advance (time/restart/ceremony),
  EvaluateOutOfPlay (direct live-ball line classification plus the goal-mouth
  verdict via GoalMouthCrossed), Consume (ball touches) and CheckPendingFoul.
  Referee has no Process compatibility entry point.
  RuleCommandSink is synchronous and write-only; Simulation implements
  Stop/StartPlay, set-piece gates, accepted ball contact, reset and phase consequences.
  A local authorization value tracks this call's stop commands, preventing a later
  sideline check from overwriting a goal-line restart. Ball/actors are live borrows
  so reset/timeout readiness samples the updated Ball at the original points.
  No stored facts/commands/config/RNG, RuntimeContext or Simulation pointer in Referee.
  Player contact extraction preserves the same regression fingerprints and twelve
  seed/order/fixture diagnostic records; goldens and legacy arithmetic are unchanged.
  PlayerId identities, a normalized [0, 1] score, a raw sliding severity and the
  victim's pitch-frame contact position; same-team contacts never produce an opponent
  foul. PlayerTripFact is deleted: Simulation resolves each assessment's identities
  and calls Referee::AssessFoul at the same instant and pair order the trip facts used
  to, so the referee judges the frozen contact instead of re-reading later actor state.
  static PlayerId -> TeamSide table. The referee now classifies out of play
  directly and BallBoundaryFact is gone.
- observation/touch_inference.* owns a read-only, RNG-free Snapshot touch
  candidate inference (TouchCandidate{PlayerId, confidence}, ordered by
  confidence, several candidates per step allowed). It takes SnapshotRecord
  pairs to refuse generation changes, non-consecutive steps and topology
  mismatches, and gates on animation touch frames plus spatial ball
  proximity; it is not wired into TouchState, actors, the referee or the
  event recognizer.
- event/touch_record.hpp + Simulation::recorded_touches_ capture each accepted
  AcceptedTouch read-only (executed step, generation, player, touch type,
  action type, with multiplicity preserved); SimulationAccess::RecordedTouchesOf
  exposes it for shadow analysis. C.1.1 diagnoses every miss against this
  ground truth: over 4000 DefaultAI steps there were 11 kicked / 28 nonkicked /
  1 accidental accepted touches, inference matched 12 and missed 28 with 0
  spurious; the dominant miss reason is "no baked touch crossing" (25)
  rather than clip change (3) - nonkicked/accidental touches have no scheduled
  touch frame. Golden and regression fingerprints remain byte-identical.
- referee/goal owns pure CrossedGoalLine(Pitch, side, previous, current), preserving
  the original triangles, strict segment endpoints, bidirectional intersection and
  legacy side-net literals. Simulation retains the Ball prediction lookahead gate per
  side, live-ball gate and goal application: AdvanceTime → first/second goal checks
  → netting fact → frame restoration → score/scorer/own-goal facts. No score or
  player state is read or written by the geometric predicate; no semantics repair
  or new goal event pipeline accompanies this extraction.
  Goal extraction also keeps the removed regression baseline harness and all twelve diagnostics
  byte-identical; Release 32/32, Debug 31/31 (excluding full-match CLI) and core-only
  builds pass. No regression baselines or baked assets are changed.

- team/possession owns read-only best-team/designated-player arbitration over already
  refreshed teams in processing order. It returns pointers, preserving the signed team
  time comparison, tie fallback, unsigned +10 float ratio and strict <0.85 hysteresis.
  Simulation publishes bestPossessionTeam then designatedPossessionPlayer at the old
  phase boundary. Both selection fields and the distinct physical ballRetainer fact
  remain Simulation-owned. Player::UpdatePossessionStats consumes Ball, the opponent
  TeamMatchState, now and the physical retainer explicitly. runtime/team_possession
  owns player-set aggregation and the pre/post-player possession phases (moved
  player/); runtime/possession owns the cross-team arbitration. Simulation iterates
  each active roster in order; Team::Process/UpdatePossessionStats/
  UpdateDesignatedTeamPossessionPlayer are deleted. Team only publishes its
  resulting data (match_state_); no Team context/services are introduced and Player
  never receives a Team&.
  Unused Team::HasUniquePossession and restart query wrappers are deleted; restart
  facts are read from rules directly. Team has no Match pointer at all; the
  former Player/Humanoid lifetime dependency is gone. ApplyBallTouch supplies
  now/retainer to first then second possession refresh before spin/notification.
- Simulation owns std::vector<MentalImage> and CaptureMentalImage scheduling. Capture
  remains after Ball processing, before players, empty-or-ten-tick, newest-first with
  three slots. StepPlayers passes a tick-local span directly into `Player::Process`,
  including the touch-selection helpers (`TrySelectAction`, `SelectBestTouchAnim`,
  `MovementSmuggle`).
  No actor stores the span or samples through Match. Preserve the initial previous-delay
  sample before updating mentalImageTime, live deviation clamps, signed millisecond
  sampling, staggered cadence and all conditional sample points. H4-B moved touch composition
  out of Match. H4-A3 deleted Match::GetMentalImage, its borrowed vector member and the
  constructor history parameter; Match (and match.hpp) no longer knows MentalImage at all.
  The history is Simulation-only, reached by actors through the explicit tick-local span.
  Match::Mirror and ResetSituation are removed: Simulation mirrors teams → Ball → images
  and resets competition facts → clears history → resets Ball → both processing rosters
  at the old mutation points. Referee invokes RuleCommandSink::ResetSituation inline
  during ordinary/ceremonial preparation and never stores the port. There is no
  delayed reset, replacement context or Simulation pointer in Referee. Sampling/refresh functions
  live in observation/mentalimage_sampling and take explicit spans/Ball. Simulation diagnostic
  sampling is the only remaining history query and sees the same objects actors sample.
  History clears at the old teardown point and its storage outlives the composition. MentalImage has no Match
  member/include/constructor or implicit world-clock/Ball reads. Capture takes tick,
  ordered player span and const Ball; age/player sampling takes now, predictions take
  now/Ball and newest refresh takes Ball. Keep live Player deviation clamps and signed
  horizon quantization: replacing those policies with frozen-only sampling is separate.
  No ObservationManager or asynchronous feedback is added.
- MatchClock owns the six former clock/counter fields and immutable half-duration
  configuration. Advance(delta, phase) receives competition phase explicitly, checks
  all arithmetic before publication, clips regulation/effective time to this half and
  returns admitted ticks. No duplicate phase, referee, actor, possession window or RNG
  lives in Clock. BeginHalf is the accepted opening contact, not phase publication;
  later accepted restarts are idempotent, ordinary dead balls stop only effective time,
  EndHalf stops both. Simulation counts entry before a possible terminal whistle.
  Match keeps forwarding reads/run gates. Simulation advances Clock then updates the
  Simulation-owned recent possession window with unchanged float arithmetic, before goals.
  Match::AdvanceTime and SwitchEnds are removed. Simulation::ApplyChangeOfEnds keeps
  first roster SwitchEnds → second roster SwitchEnds → Ball Mirror → image mirrors,
  at entry before counting/controls, without changing transient mirror flags.
  Simulation::AdvanceTime is a transitional diagnostic clock-only entry (no rules,
  actors or executed-step count); product execution remains Step-only. Clock ownership
  stays in Match for now; neither that nor ballRetainer is derived selection state.
  Each of possession/history/clock extraction preserves the removed regression baseline harness
  and all twelve seed/order/fixture diagnostic records byte-for-byte. Final Release
  33/33, Debug 32/32 (excluding full-match CLI), standalone Clock and core-only builds
  pass; no golden, policy, physics or asset change accompanies these extractions.

- referee/period owns pure PeriodElapsed(half_underway, phase, regulation, duration).
  Simulation evaluates it before passive contacts and at the original whistle boundary.
  Referee's no-argument PeriodElapsed is deleted, not retained as a wrapper. Duration
  validity remains initialization/Clock authority; only SecondHalf doubles the threshold.
  Simulation::EndPeriod preserves EndHalf → Referee::OnPeriodEnded → SetMatchPhase
  → pending end change. OnPeriodEnded takes phase/tick/kickoff spot/team explicitly,
  mutates only referee facts and never reads Match. Final time retains the legacy foul
  facts; half time clears the pending foul and schedules kickoff at +10/+30 ticks.
  Physical sides/history still change at the next Step entry, not phase publication.
  Referee has no Match pointer/include/constructor dependency. Its initialization takes
  only kickoff Team/position; phase publication executes through the write-only port.
- referee/offside owns pure GetOffsideLine(mentalImage, now, ball, defending_team_id,
  defending_side, futureSim_ms). Defending id/side, sampling instant and Ball are explicit;
  it no longer reads Match/Team. Preserve the second-deepest defender scan, the same
  in-place copy mutation, ball-ahead override, halfway zeroing and pitch clamp. Referee::
  BallTouched consumes those facts and still owns the offside-player decision list.
- referee/ball_touch_facts.hpp defines BallTouchFacts: now, touch player/team id, touch/
  defending Team pointers, in_play/in_set_piece/offsides flags, const Ball and the ordered
  all-active span. Simulation assembles them from current competition facts; the event
  dispatcher calls Referee::BallTouched(facts, commands) synchronously. Referee derives defending active
  count/candidates from the span. Restart release/offside/foul consequences use the
  same synchronous write-only rule port and explicit stadium frame.
  MentalImage decoupling, clock/end-change orchestration and period extraction each
  preserve regression fingerprints and all twelve diagnostic records byte-for-byte.
  Latest Release 34/34, Debug 33/33 (excluding full-match CLI), standalone period
  and core-only builds pass; no golden, physics, policy or baked asset changed.

Match is deleted. The composition root Simulation owns competition/clock, actor
ownership, TouchState values and the possession window; match.hpp/match.cpp and
Simulation::match() no longer exist, and diagnostics use SimulationAccess.
Remaining sim/match value types moved to runtime/clock, runtime/phase, runtime/result,
simulation_config.hpp and pitch_geometry.hpp. Never reintroduce a runtime-pointer
MatchState or a RuntimeContext for actors.

Current phase order (composed directly by Simulation):
```text
ApplyControls → ResolveBallPlayerContacts (AcceptedTouch → bookkeeping → recognizer → referee)
→ ProcessReferee (Advance → EvaluateOutOfPlay → CheckPendingFoul)
→ StepBall → CaptureHistory → StepPlayers → UpdatePossession
→ ResolvePlayerContacts (FoulAssessment → Referee::AssessFoul) → AdvanceClock
→ UpdateRecentPossession → EvaluateGoal (Referee::GoalMouthCrossed)
→ ApplyPendingRulings (AwardGoalRuling) → scorer/own-goal facts
```
Terminal-referee and ceremony early returns keep their existing frame restoration
and clock behavior; this sequence is not permission to reorder legacy phases.

Accepted-touch path: physics and actors publish one AcceptedTouch per accepted
contact; the player contact solver publishes FoulAssessment values. The referee
consumes both and decides, and Simulation applies the verdicts.
```text
Entity state → Simulation tick → AcceptedTouch / FoulAssessment → RefereeState
             → Ruling → Simulation → new state → MatchEvent
```
- event/accepted_touch.hpp + event/accepted_touch_sink.hpp own the transitional
  AcceptedTouch value (touched_at, player, team, touch type, ball pos/vel, action
  type) and its write-only AcceptedTouchSink::OnAcceptedTouch callback. There is no
  SimulationFact/StampedFact/TickFactBuffer machinery; player_contact reports
  FoulAssessment instead.
- referee/ruling.hpp owns the domain verdicts. Referee production stages are
  Advance/EvaluateOutOfPlay/Consume/CheckPendingFoul plus AssessFoul/GoalMouthCrossed;
  TripNotice and BallTouched stay
  only as direct-test adapters. Simulation::ApplyPendingRulings visits every ruling
  kind explicitly (goal, stop, restart, card) so none can be silently dropped, and
  records confirmed MatchEvents in the owned EventLog.
- Goal-line priority is preserved by classifying at most one out-of-play
  condition per instant (goal-line else touchline) directly from the live ball,
- The accepted-touch callback keeps the legacy synchronous order inside the tick;
  there is no buffering/flush guard. Touch bookkeeping, event recognition,
  transition commit and referee consumption all happen in the producer's call.
- event/event.hpp + event_transition.hpp own recognized football behavior and its
  immediate transitions. EventRecognizer opens a PassEvent/ShotEvent from an
  accepted action carried on AcceptedTouch, resolves it on the next touch, a
  timeout or a confirmed goal, and draws no RNG or world state. Simulation runs the
  recognizer on each fact before the referee and records PassCompleted/ShotEnded
  MatchEvents; the referee still judges the same immutable fact, so recognition is
  additive to the rules and cannot perturb physics or goldens.

- Start requires stopped state and initializes local owners before publishing
  either. Failure remains stopped. Stop is idempotent and releases both; Start
  recreates the original declarations/config and reuses the named process logger.
  Parsing, formatting and serialization belong to app, not GameEnv.
- One Step means one 10 ms simulation step. After full time it is a no-op including
  AI; stopped Step/Observe throw logic_error. Finished is false while stopped.
  Result throws before full time or after Stop; stopping never invents completion.
  Observe/Result return owning values that can survive teardown.
- `football::sim::Tick` is an absolute timeline instant; `TickSpan` is a duration.
  `sim/time/tick.hpp` owns the fixed 100 Hz quantum and float seconds derived from it,
  not foundation or runtime configuration. No absolute-time + absolute-time API.
  Boundary conversions live in `sim/time/tick_boundary.hpp`; non-grid milliseconds
  are rejected, never silently rounded. Absolute millisecond timeline/touch adapters
  are deleted; remaining milliseconds are continuous estimates, signed sampling,
  SI/animation formula boundaries or output projections, not second clocks.
  Keep unit migration separate from policy; preserve float arithmetic and goldens.
  MatchClock stores the sole Tick now_; Simulation advances it by TickSpan{1} each
  normal step, and WorldState publishes the tick value directly.
  Actions store only elapsed/duration/optional contact `TickSpan`; animation frame
  readers are projections, not duplicate clocks. Decision and locomotion schedulers
  use Tick deadlines and owner-local TickSpan cadences. Their tests are in
  `unittests/player/player_action_cursor_test.cpp`; old diagnostic digests project
  milliseconds. Player requeue/tactical phase cadences use the same tick-local
  policy values and overflow-safe reduced-remainder staggering, retaining both
  roster schedules.
  Player touch/card-effect timestamps are Tick; unused possession-duration storage
  is removed. Touch-decay requires an explicit Tick and bounds only relative ages.
  Player::Process consumes a stack-local PlayerTickContext (now, authorization,
  last-touch identity and a live half-underway gate). Simulation assembles it for
  EACH actor; no stored context/span/port. Fatigue reads the borrowed clock gate
  after Player action execution so accepted opening contact remains synchronous.
  Locomotion eligibility, scheduling and decision publications take explicit now/
  Ball/retaining-ball inputs. Player has no Match member/include/lookup:
  reset provenance consumes Tick, deactivation consumes Ball/Tick, and send-off
  consumes Ball/Tick/RNG. Team::Exit performs the former destructor reset before
  each deletion in the original roster order. Player constructors borrow Pitch,
  AnimationLibrary and the Simulation-owned live RNG explicitly; every actor owns
  its own body profile and playback state from construction. Resets and action
  draws
  never retrieve RNG through Match, and no stream is copied or reseeded here.
  Player::Process, animation selection, touch prediction, movement-smuggle
  and physics-vector helpers require an explicit evaluation Tick and never read
  a clock through Match. Their remaining Match reads are physics/authorization.
  Ball prediction horizons/cache durations live in sim-private `ball/ball_timing.hpp`;
  prediction generation iterates TickSpan samples with seconds from the quantum.
  MentalImage stores a Tick capture instant and derives TickSpan age. Transitional
  calculation sampling adapters retain legacy horizon quantization; the closest
  current coverage is `unittests/ball/ball_simulation_test.cpp` ("Ball Predict
  over a horizon does not mutate state"), not a dedicated adapter suite. Unused Ball/Player mean-history APIs and
  their write-only history storage are deleted, not replaced.
  Clock scale removal is a separate S2 semantic stage, not a unit-only rename.
  See the removed design notes before rounding continuous arrival estimates.
- Simulation owns period-end lifecycle and MatchClock value/lifetime. Simulation owns
  phase/score and only borrows the clock for transitional projections/commands.
  Clock contains executed-step count, Tick timeline, regulation and ball-in-play.
  MatchPhase is PreMatch/FirstHalf/SecondHalf/Finished; second-half ceremony is
  inside SecondHalf. MatchOptions::half_duration defaults to Minutes(45).
  Native duration must be positive and two halves must fit uint64 before RNG draws.
  CLI milliseconds are converted exactly on the 10 ms grid; off-grid input fails.
  No duration scale, added/extra time, penalties or CLI tick budget/--steps.
- Regulation starts from each half's accepted kickoff and includes ordinary dead
  balls. Ball-in-play accumulates only after accepted actual restart contact.
  StartPlay/World.in_play authorize a Ready taker; they are not effective time.
  half_underway/ball_in_play expose separate facts. EndHalf stops both clocks.
  Direct AdvanceTime clips football clocks at the current period; whistles win
  over pending restarts and further ball contacts. Fatigue uses real metres during
  an underway half, including dead-ball positioning, without scale compensation.
- Terminal Match freezes clocks, actors, actions, ball, scores, RNG and Result.
  duration_ticks counts executed Steps including the terminal transition; World.tick
  is the monotonic simulation timeline. Ordinary dead balls execute every positioning
  tick, without skips; the terminal whistle call does not advance the timeline.
  Clock observations use native TickSpan; no authoritative match millisecond clock.
- Half time mirrors both teams/ball/mental images once at the next canonical
  between-tick frame. Static physical sides flip, while WorldState keeps the home
  frame and TeamSide remains Home/Away. The same physical goal credits the opposite
  team in the second half. First-half numerics remain unchanged.
- `include/football/pitch/pitch_frame.hpp` provides the runtime/home-pitch
  transform (`PitchFrameTransform`, inline geometry) that observation and control
  execution apply. Team transforms derive from current dynamic side versus fixed
  Home=-1/Away=+1; the between-tick ball uses the first processing roster's frame.
  Never infer orientation from MatchPhase: SecondHalf is published before the
  pending physical change of ends. Ball position AND velocity must share the same
  home-pitch frame as player positions; defending_direction does not flip.
  Fixing the missing ball transform intentionally changes second-half AI matches,
  not first-half numerical goldens, clock policy, physics or animation resources.
- WorldState is an owning projection, not authoritative storage or a history cache.
  The unused request-only ObservationEpoch marker and Match ownership state are
  deleted. Native Player action/publication continuity epochs remain owner-local.

## Policy and execution boundaries

- DefaultAI owns only TacticalBoards; no actor pointers, transient request channel,
  hidden observation caches or RNG. Model-only MakeTacticalBoard initializes base
  roles/anchors/spacing. Update reads values and emits frame-local controls.
  AIConfig may copy initial boards by side; no mutable GameEnv board exists.
- TacticalBoard is desired/planned intent, never restart/score/current state.
  Width/depth are pitch fractions (defaults 0.75/0.55); anchors are home-frame metres.
  Update computes transient targets without rewriting boards.
- RefereeBuffer owns restart facts. Ordinary restarts use Pending → Ready → Taken
  → InPlay with entered/earliest/timeout Tick boundaries and local min/max policy.
  Ball/action reset and taker/target planning occur once, without actor teleportation;
  actors move under controls while pending. Readiness checks ball placement/movement,
  taker approach, legal opposition distance/areas and stopped actor positioning.
  `referee/restart_readiness.*` owns pure home-frame plans and geometric predicates.
  DefaultAI receives only restart_pending/per-player target values in WorldState;
  pending commands cannot request contacts. Sub-idle movement stays continuous
  during pending positioning; the legacy live-play deadband is unchanged.
  Maximum delay repairs legal positions once, never invents a contact. Actual
  scheduled taker contact releases the set piece; cursor expiry/retain anchoring does not.
  Only the authorized taker may execute native contacts before the ball is live.
  No preparation fast-forward/tail API remains. Opening/half-time ceremonies
  retain prepare/start deadlines/placement through private PrepareCeremonialKickOff,
  but share Ready/Taken release: neither a whistle nor a timer fabricates contact.
  Bounds are provisional engineering policy, not realism calibration. Richer
  quick-free-kick/wall administration and calibration remain explicitly tracked
  in the removed design notes.
- Diagnostics/tests may compose Simulation and explicit PlayerControlSet sequences
  outside GameEnv. Equal declarations + equal control tapes must replay identical
  complete WorldState payloads and RNG states, including Stop/Init replay.
- The GRF action protocol, player selection/sticky state, TeamDecisionRequest and
  app input target are removed. The app only configures and runs an autonomous match.
- The RequestAttackingRun/RequestTeamPressure/RequestKeeperRush/ResetRequests APIs,
  TeamRequests/TimedPlayerIntent and their request-only lifetime identity are deleted.
  Their only callers were retired input-style tests, not app, GameEnv or autonomous
  policy. No AI-internal replacement or speculative Coach system was introduced.
  Persistent tactics/marking directives, stateless retained-world replay and native
  reset provenance tests remain; default trajectories/RNG are unchanged.
- Simulation has no decisions/input ownership. Empty controls mean idle movement;
  control execution translates frames, resolves active model IDs and rejects illegal
  hands saves/inactive recipients. Mechanics remain in sim, not policy.
  BuildPlayerCommands/RequestCommand consume call-local PlayerCommandInputs containing
  only Ball, TouchState, restart facts, retainer and Pitch for authorization/hands gates.
  No command builder includes Match or retrieves it through Player; Player::GetMatch
  is deleted. Inputs are never retained, and save/backpass checks keep short-circuit order.
- Default policy replacement was intentionally semantic, not bit-exact Eliza
  migration. Historical goldens live in the removed golden baselines. Do not
  reconstruct actor-calling decision ports or insert dummy RNG draws to match them.

## Identity, ownership and numerical invariants

- PlayerId is caller-provided uint32 identity in player/player_definition.hpp.
  PlayerDatabaseId
  is import provenance only. No ID remapping/allocator, PlayerIndex, generic IDs
  module, TeamId or ambient actor numbering. Roster positions are local indices.
- Player holds no Team object. It owns only its own identity (team_id_/side_),
  its static formation entry and a PlayerTeamFacts projection (dynamic/static side,
  possession amounts, designated possession player) that Runtime republishes on
  every change. Cross-player reads take a borrowed std::span<Player* const> or a
  borrowed PlayerTeamView; a player never iterates its own squad.
- The runtime Roster owns the player entities and their stable order; Team keeps
  only the transitional TeamMatchState and derives runtime FormationEntries via
  runtime/formation.hpp BuildFormation. Simulation owns score/possession. Sample
  factories live in app/fixtures, never core.
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
  entries and keeps active/sent-off mirror behavior.
- Humanoid/HumanoidBase flattening is done (commit `60c7378`). Animation cache, 10 ms step,
  private phases, destruction order and appearance/reseed windows stay unchanged.
- Restart placement and readiness algorithms no longer accept/retrieve Match.
  PositionRestartPlayers takes the explicit Ball, regulation span, immutable options
  and RNG alongside the two teams/taker ids; all original prediction queries and
  ResetPosition/RNG ordering remain. PlanRestart takes Pitch, home-frame ball position
  and home-then-away active-player span. RestartPlayersReady receives Pitch directly;
  neither legality helper follows Player::GetMatch(). Referee assembles planning inputs
  from its explicit tick facts, after reset, not through a runtime-owner query.
- Officials are rules, not animated actors. Referee is unique_ptr-owned by Simulation;
  no PlayerOfficial, official profiles or animation-driven restart timing remains.
  Card administration adds 1000 ticks to ordinary restart minimum/maximum bounds;
  player card effect remains due 600 ticks after the foul. No skips implement either.
  Initial/half-time ceremonial kickoff deadlines remain unchanged. There is no
  animation/waiting-mode switch; baked animation resources remain mandatory for
  player motion/actions/contact. Historical pre-readiness policy fingerprints are
  archived in the removed golden baselines.
  Official removal intentionally changed RNG/goldens; current baselines include it.
- No ambient environment/context/RNG, ScenarioConfig, controller hierarchy, retired
  GRF binding or memory-hook checkpoints. Durable saves would need explicit values.
- Pitch is football::pitch::Pitch only (legacy geometry currently); formation runtime role
  adaptation is sim algorithm code, no XML/DB lookup.

## Animation and ongoing work

Runtime reads only `assets/runtime/animations.simanim`, via AnimationLibrary and
BakedAnimationSelector. Simulation loads/shares the library; every Player receives
a const AnimationLibrary& at construction. Team::InitPlayers
forwards the library only during creation, without storing an animation service.
Actor clip lookup/selection/root positions and foot counterfactual diagnostics use
the injected library, not Match. Player retains it for activation/reactivation; its
lifetime remains Simulation-owned. The Player's motion and animation helpers
consume explicit Ball, live
TouchState/opponent roster, evaluation tick, RNG and baked AnimationClip parameters.
They neither include Match nor retrieve it through Player; Match::GetAnimPositionCache
and the private implicit clip lookup are deleted. Shot inputs omit unused arguments.
There is no ambient animation owner or hidden load.
Offline `.anim`/XML/object parsing lives under
`tools/animBaker/`, never libsimcore.so. Two include roots are distinct:
`football/animation/...` is the shared baked format; `animation/...` under
tools/animBaker is the offline import tooling. Their filenames
are disjoint. The original XML parser, codecs and necessary token/decimal helpers
are owned by `tools/animBaker/import/legacy_*`, compiled only into the offline
`football_legacy_anim` target. Keep parser semantics, including multimap ordering,
duplicate tags and whitespace removal; do not replace it with a new XML library.

Active movement work is animation root motion → explicit procedural kinematics:
player_kinematics, player_locomotion, player_body_facing and decision/locomotion
schedulers. Pure locomotion means procedural Movement without scheduled contact or
retained ball; smuggle offsets are transitional keyframe corrections; Shot/Pass/
Trap/BallControl still animate their contact frame. Measurement-only changes must
keep regression output exact.

## Player body model (BODY-1)

BODY-1 gives one default skeleton the ability to express different heights and
builds purely from `PlayerDefinition` height + (new) weight, before any Godot
asset work. The scope is body modelling and geometry; player-player contact,
ball impulse, action clocks, root motion and AI are untouched.

### BODY-1.0 reference body dimensions audit

The shipped bind hierarchy is `data/media/objects/players/player.object`, and
`player_baked_body_pose.hpp` re-declares the same 14-node offsets (root,
body/pelvis, middle/trunk, neck, two thighs, two knees, two ankles, two
shoulders, two elbows) as versioned bind offsets; the runtime never parses the
object. Reference bind-pose heights at scale 1 (z above root z, the standing
reference):

- total height: head top ≈ 1.903 m (neck 1.61 + head center offset .1212 +
  head radius .172); the animation-scale reference `defaultPlayerHeight` is
  1.92 m, ~1.7 cm above the geometric head top — a nominal constant, not a
  measured vertex.
- hip (body/pelvis): 0.96 m
- thigh joint: 0.95 m, knee: 0.53 m, ankle: 0.09 m
- shoulder: 1.59 m, elbow: 1.26 m
- neck: 1.61 m
- standing foot lowest point: boot capsule bottom ≈ -0.038 m (ankle - .057 -
  boot radius .071), i.e. the boot volume pokes slightly below root z.

Reference capsule/sphere radii (scale 1) for the fine baked body geometry:

- torso: radius .20 (body z - .02 → body z + .17)
- head: sphere radius .172 (center = neck + {.0003, -.0121, .1212})
- thigh: radius .106 (thigh → knee), shin: radius .102 (knee → ankle)
- chest contact region: radius .233 (middle → middle + .48)
- boots: radius .071, upper arms: .072, forearms: .053, palms: .065

Three distinct geometries must not be conflated:
1. fine baked skeleton capsules (`BuildBodyCollider`, six segments) — the
   posed body against the ball.
2. coarse three-part `PlayerBodyCollider` (upperBody .22 @ z 1.11/.30, lower
   .19 @ z .60/.55, head .11 @ z 1.61) — legacy upright body volume.
3. `PlayerGroundCollider.radius = 0.36` — the horizontal *contest* radius
   (two players touch at 0.72 m, `player_contact.cpp` `distanceFactor`), not a
   body width.

The legacy scale call sites that this audit listed are gone. BODY-1.1-1.6 below
replaced them with one profile (`player_body_profile.hpp`); the current
`height`-to-geometry scaling is:

- `player_body_profile.hpp:37` — `kReferencePlayerHeight = 1.92f` (the former
  `defaultPlayerHeight`), and `:58` — `height_scale = height / kReferencePlayerHeight`.
- `player_baked_body_pose.hpp:24`/`:108` and `player_baked_contact_regions.hpp:28`
  apply `profile.height_scale` to the bind offsets and the contact regions.
- `touch_geometry.cpp:55` — `animBallHeight * request.height_scale` for the touch
  ball height; the request is filled at `player_action_selection.cpp:116` and
  `touch_match.cpp:218`.
- `Player::GetBakedPoseSample()` (`player.hpp:532`) takes no scale argument: the
  scale rides inside `PlayerBakedPoseSample.body_profile`, so
  `simulation_baked_body.cpp` (`BeginBakedBodyTick`, `CompleteBakedBodyPreparation`)
  only realizes it. Only the baked root Z is scaled; root XY stays in
  `PlayerKinematicState`.

### BODY-1.1–1.6 status: profile-driven body geometry is the single source

- `PlayerDefinition.weight_kg` is optional; `MakePlayerBodyProfile` derives
  `height_scale = H / 1.92` and `girth_scale = sqrt(M / (23·H²))`. A player
  without a weight keeps `girth_scale == 1` (the legacy geometry), so no
  fixture changes.
- `Player` caches one const `PlayerBodyProfile` at construction and exposes
  `BodyProfile()`; `GetBakedPoseSample()` takes no scale argument, and the
  legacy `defaultPlayerHeight` constant is removed.
- `EvaluateBakedBodyPose` scales joints by `height_scale` only (weight never
  moves bones). `BuildBodyCollider` / `BuildBakedContactRegions` scale radii by
  `height_scale * (1 + (girth_scale - 1) * part_weight)` with per-part weights:
  torso/pelvis/chest 1.0, thigh/shin/upper-arm 0.7, forearm/palm 0.4,
  head/boot 0.
- Body geometry is the default production CCD authority after Init (P7a
  enables prepared tick + body CCD + baked bodies); the girth path is live but
  inert while fixtures leave `weight_kg` unset, so no golden changes.
- `PlayerGroundCollider.radius = 0.36` and `player_contact.cpp`'s 0.72 m
  contest threshold are intentionally untouched.
- Tests: `unittests/player` covers profile derivation, nine-profile bind-pose
  geometry, symmetry/mirror, CCD ordering and legacy no-weight equivalence.

### BODY-1.7 Godot appearance contract (data convention only)

This repository has no Godot front-end; the contract below is what a future
asset must satisfy so C++ capsules and rendered meshes share one body profile.

- `Player3D / VisualRoot → Skeleton3D + MeshInstance3D`, using one skeleton
  compatible with the existing 14-node bind hierarchy.
- Bake a standard-girth skinned mesh plus `Slim` and `Broad` Blend Shapes.
  Animations stay shared; only the body shape differs per player.
- Godot reads the same `PlayerBodyProfile`:
  - `height_scale` → `VisualRoot` overall scale;
  - `girth_scale` → Blend Shape weight (Slim below Normal, Broad above);
  - `mass_kg` → physics-only input, never rendered directly.
- C++ capsules are not reverse-generated from the mesh: they stay the
  authoritative collision geometry and are overlaid as debug geometry for
  visual acceptance against chest/limb/head silhouettes.
- If the current assets lack skin weights or Blend Shapes, prepare them in
  Blender first; a C# scale API alone is not visual modelling.

### BODY-1.8 headless body placement validation

`unittests/player/player_body_placement_test.cpp` places nine standing-pose
players (the same zero-rotation bind pose) on a real `Pitch(105, 68)` whose
ground HalfSpace is at z = 0, and checks pitch-space geometry: root at z = 0,
joint proportions, left/right symmetry, capsule extents and the support-foot
height. Optional bounded CSV/text diagnostics via
`FOOTBALL_BODY_PLACEMENT_DIAG`.

Foot semantics, deliberately kept distinct:
- ankle joint: a skeletal quantity at ~0.09 × height_scale (0.084 m at 1.80 m),
  never forced to zero.
- boot capsule (`BuildBakedContactRegions`): ball-foot contact geometry whose
  reference bottom is -0.038 × height_scale (≈ -3.6 cm at 1.80 m), so it pokes
  slightly below the ground plane. This is not a foot-ground support solver.
- `PlayerGroundCollider` is a 2D horizontal contest circle, not foot support.

Standing acceptance: the support foot's lowest boot capsule stays near z = 0
(tested in [-0.06, +0.02] m) while the ankle stays clearly above it; no
per-frame or per-foot exact-zero requirement. `Pitch::grass_height()` = 0.025 m
is a rolling-physics parameter, not a plane offset. The reference head top is
≈ 1.9032 × height_scale (~1.78 m for a nominal 1.80 m player): `height` is the
nominal human height, not the collision-envelope top, and is documented rather
than silently re-tuned. Sampling a real Idle animation frame is deferred to the
stage-2 animation work where idle-clip selection is defined.

## Locomotion and animation adaptation (LOCO-2)

LOCO-2 adapts the existing off-ball motion (PlayerMotion/PlayerLocomotion) to
the baked Movement animation clips; no Godot, no ball actions, no player-player
contact changes. The authority boundary: PlayerMotion moves the player; the
animation adapter only selects clips/rate/phase and never writes position.

### LOCO-2.0 Movement animation audit

Source templates live under data/media/animations/movement/{idle,walk,sprint,
dribble} (+ angled/newangles); the baker expands them into a full generated
matrix. The baked library contains 1016 Movement-type clips (988 pure off-ball,
28 retain/`holdball` dribble variants). The `base_animation` flag is 0 on all
Movement clips; loopability is implied by incoming == outgoing velocity with a
straight heading (66 straight cruise candidates).

Incoming velocity buckets (m/s): idle 0 / dribble 3.5 / walk 5.0 / sprint 8.0.

Pure off-ball coverage matrix (988 clips):
- idle:    114 (90 accel-out, 24 cruise)
- dribble: 482 (142 accel, 252 cruise, 88 decel)
- walk:    320 (28 accel, 98 cruise, 194 decel)
- sprint:   72 (64 decel, 8 cruise)

Speed-transition coverage: idle->dribble 90, dribble->walk 142, walk->sprint 28,
walk->dribble 144, dribble->idle 88, walk->idle 50, sprint->walk 22,
sprint->dribble 20, sprint->idle 22.

Heading-turn coverage (pure, nearest bucket): 0 deg 172, 45 deg 298, 90 deg 242,
135 deg 164, ~180 deg (a178) 112, for both mirror directions. Body-angle turns
(b+-44/b+-45/b+-135) are present across the same matrix, encoded in
outgoing_body_angle.

Coverage verdict: idle, straight cruise, accel ramp, decel/stop and 45/90/135/
180 heading changes are all present, so the adapter can be reference-driven
rather than requiring a new parallel state machine. Open for LOCO-2.2+: mapping
physical speed between buckets to a playback rate, and choosing transition vs
loop clips by speed trend.

### LOCO-2.1 off-ball locomotion baseline

`unittests/locomotion` drives the real PlayerMotion::StepContinuous +
PlayerBodyFacing at 100 Hz with no Simulation/animation asset, over ten
scenarios: stand, accel to max, 2/6 m/s cruise, brake to rest, 45/90/180 deg
turns, slalom, sideways body observation, bit-identical replay and two
capability profiles. Contracts pinned: speed never exceeds max_speed, the
asymmetric approach never exceeds acceleration up / braking down, the velocity
direction turns at no more than maxTurnRate, no NaN and no per-tick position
jump.

Behaviour pinned for the animation adapter: the locomotion `facing` field is
frozen to idleFacing below idleSpeedThreshold (0.5 m/s) and adopts the
(already rate-limited) velocity direction once the actor is moving, so `facing`
can jump when an actor starts from a standstill. The physically continuous
trajectory is the *velocity direction*; the adapter must key body/hip turn off
velocity direction, not off the frozen `facing` field. Different motion
capabilities produce different trajectories under identical control (M1).

### LOCO-2.2 read-only adapter interface

`player_locomotion_animation.hpp` defines `LocomotionAnimationInput` (a value
snapshot of current/next kinematic state + desired velocity + body profile)
and `LocomotionAnimationOutput` (clip id / playback rate / frame time / gait
phase / switch suggestion). The output never carries a world position and
cannot write PlayerKinematicState: PlayerMotion stays the sole position
authority. This is the boundary LOCO-2.3+ implement against, not a behaviour
change.

### LOCO-2.3a pure selection strategy

`locomotion_animation_strategy.hpp` adds `SelectLocomotionAnimation`: it reuses
`BakedAnimationSelector` for the coarse Movement/incoming-velocity filter, then
ranks survivors with a deterministic score across three regimes driven by
desired-vs-actual speed (accel / decel / cruise). Cruise ranks on continuous
clip reference speed, accel/decel on the clip's outgoing velocity, and a
secondary turn term matches the requested heading change; clip id is the
stable tie-break. Near-zero speed uses bodyFacing as the direction reference
instead of normalising a zero velocity. Pure function, no behaviour change
until LOCO-2.3b wires it in; tests use synthetic clips (no asset).

### LOCO-2.3b wired Movement reorder (behaviour change)

`Player::CommitLocomotion` now captures the committed tick's
`LocomotionAnimationInput` (current/next state + requested velocity + body),
and the Player's Movement selection reorders the already-valid
candidate set so the strategy's preferred clip wins, only when the action is
pure procedural locomotion (`IsEligibleForProceduralLocomotion`). No new
candidates are added and `GetBestCheatableAnimID`/non-Movement paths are
untouched. `Simulation::EnableLocomotionAnimationAdaptation` (default ON) is
the controlled A/B switch; the runtime harness asserts the ON/OFF committed
positions and velocities are bit-identical, so the reorder never moves the
player. Ball state is deliberately NOT compared: different body poses may
change body CCD, which is the intended separation from the motion authority.

### LOCO-2.3c reference playback rate recording

The wired reorder records `LocomotionPlaybackAudit` (samples, reference-speed
and playback-rate sums/max) without advancing the integer `frameNum` clock.
Non-integer frame sampling and animation/physics time separation remain
LOCO-2.6. The runtime harness asserts the audit accumulates finite samples
while the frame clock stays untouched.

### LOCO-2.4/2.5/2.7 headless off-ball acceptance

`unittests/loco_animation` drives the strategy over the real baked library for
idle / start / cruise / brake / 90 deg / 180 deg and verifies the expected
Motion class per scenario (idle->idle, accel out of idle, steady walk cruise
near 5 m/s, decel to idle, ~90 deg and ~178 deg turns) plus determinism. It
also proves a PlayerBodyProfile changes body geometry (hip/knee/neck scale
with height) while the animation choice is identical, i.e. the animation
library is shared and capability-independent. Optional diagnostics via
`FOOTBALL_LOCO_ACCEPTANCE_DIAG`.

### LOCO-2.6 redefined: pose blending and interruption, integer frames kept

LOCO-2.6 is no longer about variable playback rate. The simulation stays at
100 Hz, the baked clips stay at 100 Hz (one integer frame per tick), and
Replay never changes PlayerMotion/Ball/rules. The new target is a continuous,
deterministic final pose across frequent action switches.

- `football/animation/pose_blend.hpp` (LOCO-2.6a): `BlendPose(source, target,
  weight)` — quaternion SLERP on the shortest path, linear root Z, exact 0%/
  100% endpoints, no NaN, no position authority.
- `include/football/animation/transition.hpp` (LOCO-2.6b): `AnimationTransitionState`
  advances one tick at a time; every transition starts from the previous
  tick's final blended pose, so a mid-blend interruption never snaps back to
  the original clip's first frame. The action cursor stays int and is
  not deleted or retyped.

LOCO-2.6c wiring points (wired): a new clip is installed at every clip-change
site, and the transition advances exactly once per tick, after the selection
settles and before `Player::SampleBakedPose` consumes the final pose. The
"not yet wired" list this note used to carry is superseded by the pose-blending
section below.

### LOCO-2.6c/d wired pose blending

`Player` now owns `AnimationTransitionState`, the final blended pose and the
clip-change guard: `Player::UpdatePlayback` drives `PlaybackState::Tick` with
`football::animation::BlendTicksForSwitch`, so the transition advances exactly
once per tick. Pure Movement blends across a clip change from the previous final
pose (mid-interruption safe); everything else copies the raw pose and resets the
transition. `Player::SampleBakedPose` returns the final blended
pose, so the body/CCD tape derives from the same single pose. Integer
action cursor stays integer and is not retyped. `Simulation::EnableMovementPoseBlend`
(default ON) is the A/B switch; the runtime harness asserts ON/OFF committed
positions and velocities are bit-identical.

The transition state now blends the root base yaw too (via
`AnimationTransitionState::blended_base_angle`, interpolated along the
shortest path), and `SampleBakedPose` reads `final_blended_base_angle_` during
a transition. `loco_animation_base_angle_test` verifies both the raw-kernel
boundary (BlendPose alone is local-only) and the fixed transition behavior
(the world bearing advances continuously toward the target base angle).
`ResetPosition` also clears `movement_transition_`/`final_blended_pose_`/
`final_blended_base_angle_` so re-placement never restarts a blend from a
pre-reset pose.

### LOCO-2.7 headless per-tick replay

`unittests/loco_animation/loco_animation_replay_test.cpp` scripts an
idle/walk/sprint/brake/90-turn sequence, drives selection + transition state +
body geometry per tick for the nine body profiles, and verifies: identical
animation selection for every body, deterministic bit-identical replay, finite
and rate-limited pose continuity (no teleport) across switches, and hip
height scaling with height_scale only. Diagnostics via
`FOOTBALL_LOCO_REPLAY_DIAG`.

The blend->body-CCD path is verified headlessly by
`loco_animation_ccd_consistency_test` (blend ON vs OFF changes the committed
body geometry during a switch). A fully Simulation-driven switch test is
deferred to CTRL-1: the bounded harness keeps players in the ceremonial/idle
state, so a `PlayerControlSet` movement tape does not drive locomotion (the
tested player's position stays constant) and no clip switch occurs. The
existing `EnableMovementPoseBlend` runtime A/B (empty controls) still proves
ON/OFF keep the committed motion bit-identical.

### LOCO-2.8 legacy root-motion audit (read-only, no deletion)

The pure-Movement position authority was already removed in S0/P2a:
`Player::UsesProceduralLocomotion(ball_retainer)` gates both
`Player::CalculateSpatialState` (early-returns; only `spatialState.foot` is set) and
`Player::ProjectMovementState` (the pure branch only commits + projects;
non-procedural keeps root-motion adoption). So `anim_state_.positions`,
`movementSmuggle` and `actionSmuggle` are NOT read on the pure-locomotion
position path.

Classification:
- Category 2 (keep, non-Movement still needs): `CalculateSpatialState`
  non-procedural branch, `ProjectMovementState` non-procedural branch,
  `CalculatePhysicsVector` (touch/trap/ball-control prediction via
  GetBestCheatableAnimID + CalculatePredictedSituation), `anim_state_.positions`,
  `anim_state_.frameNum`, `movementSmuggle`/`movementSmuggleOffset`,
  `CalculateFactualSpatialState`, `CalculatePredictedSituation`.
- Category 3 (compatibility projection, no authority): the Movement-branch output
  of `CalculatePhysicsVector` (`positions_tmp` -> `anim_state_.positions`,
  `rotationSmuggle_tmp`) and the `CalculateMovementSmuggle` write for Movement.
  These are written but never consumed by the pure path, and they share the same
  functions as category-2 callers.
- Category 1 (pure-Movement-dead, safe to delete as a self-contained unit): none
  found.

Conclusion: LOCO-2.8 is not a clean category-1 deletion. The remaining work is
"simplify category 3" (split `CalculatePhysicsVector`/smuggle writers by Movement
vs action) plus a later state-cohesion cleanup; `facing`/`speed` compatibility
reads are explicitly left for the next state-cohesion refactor. Deferred to its
own focused change after a reviewed audit.

### LOCO-2 completion status

The redefined LOCO-2 (off-ball motion, animation selection, pose blending and
continuity) is complete: LOCO-2.0..2.7 are implemented and committed, and the
LOCO-2.8 audit proves the pure-Movement legacy root-motion position authority
was already removed by the `UsesProceduralLocomotion` gate. Milestones M1
(motion physics), M2 (animation matches motion), M3 (blend/interruption
continuity) and M4 (nine body builds share animation, geometry follows the
final pose) all pass at the headless/runtime level. Every behaviour change
ships with an A/B or controlled regression proving Player position/velocity
stays bit-identical under animation-selection or pose-blend changes.

Deferred follow-ups (separate, reviewed work): the optional category-3
simplification of shared root-motion writers, and the `facing`/`speed`
compatibility state-cohesion refactor.

## Player kinematic state cohesion (STATE-1)

Target: converge `PlayerKinematicState` from five fields (position, velocity,
facing, bodyFacing, speed) toward three (position, velocity, bodyFacing), with
no second motion authority. `speed` is redundant and removed first; `facing`
carries the low-speed direction reference, so its removal is a separate
physics-semantics decision (plan A: derive from control at standstill, plan B:
keep as locomotionHeading).

### STATE-1.0 baseline (test-only)

`unittests/locomotion/locomotion_state_baseline_test.cpp` pins static-start,
reverse-after-brake, slalom and bodyFacing-independence, and measures the
critical risk for STATE-1.1: the stored `speed` and `velocity.GetLength()` are
NOT bit-identical (the normalized direction is not exactly unit length, so
`dir * speed` differs from the scalar by ~1e-8). STATE-1.1 recomputes
GetLength() and accepts this last-bit-only change; the baseline bounds it
below 1e-6.

### STATE-1.1 speed removed (derived)

`PlayerKinematicState.speed` is deleted and replaced by the inline `GetSpeed()`
= velocity.GetLength()`. Writers (`PlayerLocomotion::Step`,
`PlayerKinematics::Step`, `CommitLegacyActionMotion`) no longer store the
scalar, and readers derive it. The old bit-exact mirror oracle clause
`FloatBitsEqual(speed, |velocity|)` is gone; it would otherwise assert-fail
during real turning movement (~1e-8 divergence), so removing `speed` fixes a
latent Debug assertion. Facing remains untouched (STATE-1.2).

### STATE-1.2 facing removed (derived GetFacing)

Reopened after the audit below, then completed in four cuts (1.2b-e).
`PlayerKinematicState` is now exactly position/velocity/bodyFacing, with:
- `GetSpeed() = velocity.GetLength()`.
- `GetFacing() = velocity.Get2D().GetNormalized()` while `GetSpeed() > 1e-4`,
  else `bodyFacing` (normalized, falling back to -Y). The tolerance is a
  numeric-zero epsilon, NOT the 0.5 m/s idle threshold: a 0.3 m/s player is
  moving and its heading is the velocity direction.

Consumers were migrated by role: `PlayerLocomotion`/`PlayerBodyFacing` use
`GetFacing()` (the stand-still start reference is now the body facing);
`GetDirectionVec`, action volume, legacy command, `CommitLocomotion`, the
`SpatialState` direction projection, `WorldPlayerState.facing` and the
player-player contact geometry all read `GetFacing()`. `Mirror()` still mirrors
position/velocity only, so the derived heading mirrors through velocity while
moving and stays on the un-mirrored body facing at rest — a deliberate
semantic change (not a pure field deletion). The observation layer keeps its
own derived `facing` for AI. Facing writes and the bit-exact facing oracle
clause were deleted with the field.

STATE-1.2 follow-ups (P1/P2 from review):
- P1 (fixed): `Player::ResetPosition` now sets `bodyDirectionVec`/
  `bodyAngle` from the locomotion direction (the focus-derived `startAngle`),
  so `bodyFacing`, the animation base and `GetFacing()` all agree after a
  reset instead of body facing hard-coded world -Y. Verified by a non-default
  focus runtime test.
- P2 (tested, not changed): `Mirror()` still mirrors position/velocity only,
  so the derived `GetFacing()` mirrors through velocity while moving and stays
  on the un-mirrored body facing at rest. A pure test pins this.
- P2 (deferred interface cleanup): `PlayerLocomotionInput.idleFacing` is no
  longer read by `PlayerLocomotion::Step` (the stand-still reference is now
  `GetFacing()`), and the dead `PlayerKinematics::Step` still takes unused
  `desiredFacing`/`maxTurnRate`. Both are interface tidy-ups for the CTRL-1
  input-adapter work, not new direction state.

### STATE-1.3 legacy projection audit (read-only; no behaviour change)

The boundary is now explicit and gates cleanly on `UsesProceduralLocomotion()`:
- Pure Movement: `ProjectMovementState` -> `CommitLocomotion` (Player is the
  sole authority) -> `ProjectSimulationMovementState` (one-way projection into
  `SpatialState`, no adoption back). `CalculateSpatialState` early-returns and
  only sets `spatialState.foot`.
- Non-Movement (legacy action): `CalculateSpatialState` derives root-motion
  `SpatialState` -> `CommitLegacyActionMotion` (adopts into Player) ->
  `ApplySimulationMovementState` re-projects + adopts again (idempotent).

Field ownership (spatialState):
- position/movement/floatVelocity: pure = projected from Player; non-Movement
  = root motion computed in CalculateSpatialState, then re-projected.
- directionVec: pure = state.facing; non-Movement = derived from movement/body
  direction, then re-projected.
- actualMovement/physicsMovement/animMovement: non-Movement = computed in
  CalculateSpatialState; pure = set to velocity (compatibility, no pure
  producer).
- bodyDirectionVec/bodyAngle/relBodyDirectionVec/relBodyAngle: written by
  `ApplySimulationBodyState` from `state.bodyFacing` on both paths;
  rel-body is animation-selection-only quantization.
- angle/previousPosition2D: projected for the next movement derivation.

Conclusion: the two paths never overwrite each other within one tick (the gate
is exclusive), and no field has two live writers on the same path. No code
change is made in this cut. Deferred, recorded for later work: the redundant
second `CommitLegacyActionMotion` on the legacy path, further shrinking of
`SpatialState` rest fields, and a future `facing` -> `locomotionHeading` rename.

## Player control authority migration (CTRL-1)

Goal: let pure Movement consume `PlayerControl` directly instead of the legacy
Decision-Queue/PlayerCommand cache, without deleting the queue (it still
selects ball-action candidates). The three-field `PlayerKinematicState` is
kept unchanged; CTRL-1 adds no new state.

### CTRL-1.0 control -> command -> motion audit (read-only)

Current chain, in order:
1. `DefaultAI::Update` emits a `PlayerControlSet` of
   `PlayerControl{move_direction, desired_speed, look_at, action, ...}`.
2. `Simulation` dispatch applies it via `Player::SetControl` (home-frame
   values; `BuildPlayerCommands` converts to the runtime frame at consumption).
3. On the decision-clock cadence (periodic or continuity repair),
   `Player::RequestCommand` -> `BuildPlayerCommands` builds a Movement command
   directly from the control (`move_direction`/`desired_speed`/`look_at`) and,
   when `action != None`, an action command; the queue is `{action, movement}`.
4. `Player::PublishPlayerDecisionQueue` stores the queue + generation and
   commits the scheduler.
5. On continuity repair only, `PublishMovementIntentFromQueue` picks the first
   Movement+useDesiredMovement command and stores it as the decision locomotion
   intent (epoch-stamped).
6. `Player::ProjectMovementState`'s pure branch reads
   `GetDecisionLocomotionIntent()` -> `Player::CommitLocomotion` ->
   `PlayerMotion::StepContinuous` -> `CommitKinematicState`.

Findings for CTRL-1.1/1.2: `BuildPlayerCommands` already derives the Movement
command straight from `PlayerControl` (its `desiredDirection` fallback is
`GetDirectionVec()` = `GetFacing()`, consistent with STATE-1.2), so a direct
control->motion adapter can reuse it and skip the queue for pure Movement. The
queue remains the action-candidate/fallback path. The Movement intent is
currently refreshed only on epoch change, so CTRL-1.2 (per-tick control
consumption) is the deliberate response-frequency change; CTRL-1.1 is
data-channel only.

## Player animation runtime (ANIM-1)

Goal: a stable per-player animation runtime so an actor keeps advancing its
animation without new AI input, can switch actions actively, can be
interrupted by real physics, and every active player produces one final pose
per tick that physics and snapshots share. No GNN, no future-action queue, no
ragdoll, no asset-format change, no C++20 modules.

ANIM-1 moved that runtime into `Player` and deleted the legacy
`Humanoid`/`HumanoidBase` types together with the `src/player/humanoid/`
subtree (commit `60c7378`, A9.5-A9.9; `905c808` dropped the last compatibility
state and stale comments). The step-by-step migration log (A1-A9) that used to
live in this file is in git history; the current implementation is `src/player`
plus the stateless `football::animation` module.

Rules that still govern the code:

- **One owner per fact.** The Player owns the action clock (`PlayerActionState`),
  the playback cursor and transition, the final pose and the spatial state
  (`anim_state_`/`spatial_state_`); `football::animation` is stateless (baked
  library + selector + `SamplePose` + `BlendPose`). There is no second stateful
  animation runtime and no `Humanoid` type: the name survives only as parameter
  and diagnostic-counter naming (`player_diagnostics.hpp`).
- **One final pose per tick.** Every action type advances the pose exactly once
  per tick and shares it with physics and snapshots. Reading the pose
  (`Player::Pose`, `Player::GetBakedPoseSample`) advances nothing and consumes
  no RNG.
- **Action arbitration is pure and Player-owned.** A one-shot in flight rejects a
  repeat of the same action and defers a different one until a legal switch
  window; a physics reaction always preempts. Every reset path (`ResetPosition`,
  `ResetRuntimeState`) clears the active and passive channels. Selection,
  installation and dispatch are separate units (`Player::TrySelectAction`,
  `Player::InstallSelectedAction`), and the passive trip decision and its local
  queue are Player-owned.
- **Blending never moves the contact instant.** `BlendTicksForSwitch` is 8 ticks
  for pure locomotion and 4 for a switch involving an action, while the touch
  FRAME stays the integer action cursor. The blended pose is what the body
  contact geometry reads.
- **Pure calculations live off the Player.** `PlayerMotion` and
  `football::animation` own the action displacement and body state, the per-tick
  smuggle evolution, the candidate reference-motion kernel, the touch-selection
  loop and the goalkeeper boundary clamps: explicit inputs, no RNG, no clock
  read.
- Tests: `unittests/player`, `unittests/animation`, `unittests/locomotion`,
  `unittests/loco_animation`, `unittests/player_animation`, plus bounded cases in
  `unittests/runtime`.

## Conventions

C++23, extensions off, PIC on. Namespaces: football::math (foundation math/RNG),
football::geometry, football::collision, football::player/sim/pitch/ball/animation,
football::ai, football::app; the offline baker uses football::anim_baker. Legacy
sim/value classes remain global. Never duplicate
an authoritative field as a cache without need. Preserve Mirror/reverse processing
symmetry. Model must not include simulation or orchestration headers.

### Player observation time after T4b

Player publication/reset provenance uses optional Tick (unset is not tick zero);
re-entry age sums/maxima use TickSpan. The unused decision-queue shadow, stale
millisecond accessors and write-only per-query trace fields are deleted; the sole
PlayerDecisionQueue and continuity/oracle contracts remain. Tactical image delay
is computed only at the sampling call, not cached as Player state. Simulation's native
history API and explicit tick-local actor spans use TickSpan and nearest-capture half-up
sampling; the ten-tick cadence and signed millisecond adapter preserve legacy
reaction rounding. Empty history is an explicit logic_error, not an invalid pointer.
Continuous reachability/reaction estimates and the Player's SI/animation arithmetic
are not integer-grid deadlines; their remaining calculation migrations are separate.
T4b must preserve existing physical/RNG goldens and full-match restart metrics.

### Reachability search time after T4c

Locomotion prediction/arrival/intercept rollouts take TickSpan horizons and pass
TickSpan to target queries. Their discrete results are optional TickSpan (absent
is not tick zero). Hybrid exact horizon must not exceed the total horizon; equal
horizons perform no extra analytic candidate. Endpoint checks precede increments
so maximum spans cannot wrap. Analytic arrival estimates and mixed Player possession
rankings retain millisecond precision; horizon comparison never floors the estimate
or multiplies a huge duration into milliseconds. The legacy action reachability
search also owns a TickSpan cursor/optional horizon; its adaptive round-then-floor
grid and strict crossed-limit behavior remain unchanged. Independent Step-based
candidate rollouts live in unittests/reachability_tick_test.cpp. T4c changes no policy.

### Native timeline callers after T4d

GetActualTime_ms, BumpActualTime_ms and GetLastTouchTime_ms are deleted, not kept as
compatibility APIs. Touch-decay readers require an explicit Tick evaluation instant;
there is no implicit-now overload. The ability-dependent decay
formula retains its off-grid millisecond precision, using only a bounded relative
age projection. It never converts the absolute timeline into milliseconds. Old
diagnostic wire units are projected at output-only call sites. The unused possession-
side history/accessor and its sole ValueHistory implementation are deleted.

### Typed reaction sampling after T4e

History sampling accepts TickSpan for grid ages or std::chrono::milliseconds
for signed continuous reaction estimates; the untyped int overload is removed.
Chrono durations here are calculation values, never simulation instants, physics dt,
schedulers or clock accumulators. The Player's mentalImageTime is a typed reaction
delay with intentional previous-delay/current-delay stages, not a redundant timestamp.
Keep its ordering; observation/mentalimage_sampling rounds to capture slots. It clamps before integer
narrowing and handles the full signed duration range. Physical/RNG rows and complete
seed 42/43/44 restart records remain unchanged after T4c–T4e in all tested modes.

### First-roster execution frame correction

Between ticks the ball shares the first processing roster's frame, including
reverse processing. Referee/collision/first actor scopes turn only the OTHER roster;
the second-actor all-actor turn is restored with an unconditional ball turn at exit.
Possession rollout uses the same geometry. Referee no longer owns a special ball
compensation adapter. The intentional reverse-row change and causal pre-fix failure
are recorded in the removed golden baselines. Native reachability covers nonzero
ball positions in both halves/orders; long two-half tests pin actual open-play
kicks and exact replay, not desired scores. Realism/behavior calibration is not complete.

### Snapshot history — Stage 1

`sim/observation/snapshot.hpp` defines dynamic Ball/Player values, fixed match-wide
PlayerId slot metadata, and separate step/timeline/reset-generation stamps.
`snapshot_history.*` is owned by football_sim and exported through its HEADERS,
not the AI-facing sim_contracts target. The standalone
`football_snapshot_history_test` compiles storage with value dependencies only.

SnapshotHistory defaults to 60,000 samples (ten minutes at 100 Hz). Reset must
preallocate each slot before Record; successful writes allocate nothing. Slots
are never compacted when inactive. Steps strictly increase; timeline ticks and
reset generations are nondecreasing, allowing duplicate ticks and jumps without
inventing missing samples. FindStep and latest-exact Find(tick, generation) use
binary search in chronological ring order. CopyLatest returns owning records in
oldest-to-newest order; insufficient history leaves its output unchanged.
Clear invalidates queries but retains all storage. Borrowed record pointers must
not cross slot overwrite, Clear, Reset, teardown or asynchronous consumers.

### Snapshot capture — Stage 2

Simulation::Step wraps the unchanged StepImpl phases and commits one Snapshot
after every successfully executed step, including ceremonial and terminal early
returns. Init preallocates history/scratch and stable home-then-away runtime actor
slots before publishing owners, then records step zero. MatchOptions::snapshot_capacity
configures retention (positive, default 60,000); it never changes execution/RNG.
ResetSituation preserves history with new generation on the next sample; manual
AdvanceTime creates no samples. Finished retains history; Stop releases it and all
actor borrows. SnapshotMetadata owns pitch and FNV-1a-64 of loaded animation bytes.

Capture uses Ball::state and Player kinematics/action projections, with the same
ToHomePitchFrame rotation as WorldState, including ball angular velocity. Important:
WorldState still reads the legacy Predict(0) position cache, which can lag the
authoritative ball position; do not refresh it or change policy to force equality.
Only created runtime actors have slots (current Team creates formation entries,
not omitted bench profiles); inactive slots stay and carry no actor state. The
bounded runtime harness (`unittests/runtime`) covers actor/slot lifetime, mirror
and Stop/Init replay; the Release-only integration test
(`unittests/integration/autonomous_match_test.cpp`) covers both halves and the
ceremonies, terminal duplicate ticks, send-offs, unequal rosters, jumps and resets.
Disk archive and temporal consumers remain separate stages. Snapshot is a
read-only record, never a WorldState replacement or restart checkpoint.

### Snapshot archive — Stage 3

app/recording/snapshot_archive.* is owned by the EXCLUDE_FROM_ALL
football_recording target, linked only by app and archive tests, never by libsimcore.
It uses Threads::Threads privately and model/value contracts publicly. Open/Append/
Flush/Close are single-producer APIs; the worker owns disk/index/chunk state.
Append copies into a bounded preallocated queue (default 1,000 frames). Block
applies explicit backpressure; Fail aborts/reports overflow without a completion
footer. Failures surface through Append/Flush/Close and Close always joins.
The offline reader requires the versioned/checksummed header/index/footer by
default. Explicit RecoverPrefix validates whole chunks and exposes only a valid
chronological prefix, marked incomplete. It never supplies live runtime queries.
See the removed design notes for exact little-endian fields/CRC and safety limits.

GameEnv::CopyLatestSnapshot reuses caller-owned storage; SnapshotMetadata returns
an owning value. Neither exposes actors/history pointers or resamples Simulation.
CLI --snapshot=PATH opts into complete-session recording (including step zero);
--snapshot-capacity=N controls only RAM retention. Close precedes successful Result
output. Normal closed-session metadata is not a full-match checkpoint or proof
of a referee terminal whistle. Explicit Flush is stream flush, not power-loss fsync.

### Snapshot consumers — Stage 4

SnapshotHistory::ForEachStep is a no-copy/no-allocation synchronous ordered range
visit with exact executed-step bounds; missing samples are omitted. Custom moves
leave the source empty/queryable rather than retaining stale size/index counters.
GameEnv::CopySnapshotWindow provides owning memory windows and never reads disk;
EventTrajectories returns owning summaries published with the latest committed step.

EventRecognizer optionally annotates action windows with executing step/generation.
Simulation supplies the annotation only inside StepImpl, clears it even on failure,
and calls PublishTrajectories only after CaptureSnapshot. Resolved pass/shot windows
produce additive EventTrajectory values (positions, path length, peak speed, sample
count and completeness). Missing history, resets and gaps cannot invent movement.
No contact classification, timeout, goal/ruling/EventLog, physics or RNG policy
changes; diagnostic facts outside Step do not claim snapshot evidence. Referee
continues using immutable contact evidence, never post-step samples as exact touches.

observation/temporal_graph.* implements value-only GraphBuilder over owning windows
or the same in-memory history. Active radius-based directed spatial edges and
same-slot forward temporal edges preserve PlayerIds/animation fields. Inactive slots
stay masked; no temporal edge spans reset, skipped steps/ticks or duplicate ticks.
There is no existing GNN framework/model in this repository: this stage supplies
its graph-data boundary, not a trainer, scheduler or policy replacement. MentalImage
and DefaultAI remain unchanged. football_snapshot_consumers_test compiles with
value dependencies only (no sim runtime, actors, AI or disk). Native two-half tests
compare different retention capacities step-by-step with identical physics/RNG.

Final Snapshot validation: Release 41/41 (including the full regulation CLI),
Debug 40/40 (excluding full-match CLI), focused ASan+UBSan+leak checks 3/3,
and core-only builds pass. The shared core exports no SnapshotArchive symbols.
Existing regression goldens/assets are unchanged; each implementation stage
is committed locally, with no push.

### P4 body collision diagnostics

`player/player_body_collider_motion.hpp` converts the existing torso/lower
capsules and head sphere with stable dynamic ids from fixed match slots.
`player/player_body_collision_shadow.hpp` holds pure linear prediction and
read-only error/touch reports. Simulation owns id -> PlayerId/body-part mapping.
the removed body-shadow diagnostic measures native
runs; definitions and reports are in the removed body-shadow diagnostic. Geometry and
shadow replay/frame tests live in unittests/player_body_collider_motion_test.cpp and
unittests/player_body_shadow_test.cpp. These inputs never reach production Ball yet.
Use CTest or executables at build/release/, not old build/release/unittests/ binaries.
No extra Player execution, RNG, touch/rules writes or Golden refresh.

P4c Ball accepts borrowed dynamic colliders through BallTickInput and returns
physical BallStepResult evidence; predictions remain static-only. Contacts expose
normal impulse and overlap correction separately from rule touches. Body material
is explicit and provisional (.35 restitution/.45 friction), not realism calibration.
`sim/simulation_body_shadow.cpp` runs opt-in static-only/full-world production
Ball kernels on one diagnostic owner. the removed body-shadow diagnostic reports
sources/TOI/owners/impulses/state deltas against static and actual production.
Default Simulation still supplies no dynamic bodies: production authority, action
and referee ordering are unchanged. Completed shadow evidence is bounded to one
interval; resets discard pending intervals. Tests cover both orders/end changes,
post/ground priority and full-shadow vs disabled state/RNG replay. The standalone
`football_ball_allocation_test` measures input-buffer reuse with no contacts.
P4d switching and P5–P7 remain pending; current native tape has no sliding coverage.

P4d-1.2 `player_body_contact_diagnostics.hpp` classifies pre-sweep action windows,
legacy filtering facts and fixed-slot geometric/impact episodes. None is a rule
touch or a production filter. `player_body_pose_shadow.hpp` proposes low Sliding/
Trip shapes on a separately labelled full-kernel branch, keeping upright evidence
for comparisons; it requires an explicit common-frame pose axis, not blindly
mirrored PlayerKinematicState::bodyFacing. Fixed schedules/scenarios/continuous
tapes live in unittests/player_body_contact_diagnostics_test.cpp. Native reports still
show 152/26 upright nonzero responses, mostly continuing initial penetration.
The moving-body continuous tape finds a zero-impulse projection freeze: no-
remainder initial overlap prevents a separating ball from making progress. Its
passing characterization test pins a known switch blocker, NOT readiness.
See the removed body-shadow diagnostic for attribution, provisional pose limitations and
the P5a shadow-only next gate. No production Ball API/kernel/authority changed.
P4d-1.2 validation: Release 42/42, Debug 41/41 (without full regulation CLI),
including existing regression/A-B modes, passed with no Golden/asset changes.

P5a adds read-only active-touch candidates. `football/ball/ball_impulse.hpp`
is a pure `BallImpulse{impulse, contact_point}` value. The Player observes the
existing scheduled/controlled contact around `ApplyBallTouch` (action, animation
id, contact/elapsed frames, pre/post ball state, baked desired ball center,
player position) through a nullable `active_touch_shadow` sink on
PlayerTickContext; pending frames and authorization/distance/height rejections
are separate stages, so a pending animation is never an accepted touch.
`sim/simulation_active_touch_shadow.cpp` mirrors the second processing team into
the pitch frame and records against the predicted passive endpoint
(`body_physics_shadow_tick_.unified.state`), reporting same-part/other-player
passive conflicts, surface-projection fallback and the impulse response from
`ApplyImpulseAtPoint`. It never recomputes an action, draws RNG or writes state.
Native evidence shows the legacy direct spin (`SetRotation`) is not reproducible
by a point impulse (`dw` up to ~446 rad/s), so P5b must derive outgoing spin
physically. `BallTickInput` still has no active impulse and production still
passes no dynamic bodies; the production switch stays closed.

P4e separates position correction from impact in `AdvanceBallTick`. A contact
with TOI zero whose relative normal velocity is separating or resting is an
instantaneous projection reported with a zero normal impulse and
`position_corrected`; it no longer consumes the tick, so the ball keeps its free
motion and can still find one genuine impact in the same interval. Only an
impulse-bearing contact ends the tick, so at most one new impact is reported per
tick while corrections may repeat (bounded by `colliders + 2`). This removes the
moving-body freeze where a separating ball was projected onto the moving START
shape every tick. Production supplies no dynamic bodies, so pitch behavior and
regression baselines are unchanged; the moving-body trajectory test now asserts
real release instead of pinning the failure.

### Historical intermediate stages (superseded by the current P5e notes below)

P5b defines the endpoint-impulse contract and its arbitration, with the
production switch still closed. `BallTickInput` carries an optional
`BallImpulse active_impulse`, applied inside `Ball::Step` after passive motion
and netting; `BallStepResult` echoes it. It changes velocity and spin about the
supplied point only, never advances position again, and never runs a second Step,
matching the tick-endpoint timing of the legacy touch. `player/
player_active_impulse.hpp` holds a pure arbitration contract: drop candidates
whose owner+part already produced this tick's passive impact, then choose the
largest velocity-change magnitude (`|J|/mass`) with lower PlayerId and lower body-part enum as stable
tie-breakers, so at most one active impulse exists per tick and permutation
cannot change the winner. Production constructs no active impulse and keeps
using the legacy touch path; the removed regression baseline harness is byte-identical.

P5c-1/P5c-2 prepare the active-touch production migration and keep it off.
`the removed design notes` records the exact former `StepImpl`/`Humanoid::Process`
dependency table
where `ApplyBallTouch + SetRotation` take effect) and the plan: split
the former `Humanoid::Process` into one prepare pass (motion/animation frame/touch vector)
before the single authoritative `Ball::Step` and one commit pass after, never
running a second Process or delaying by a tick. The Shadow also runs the pure
model on captured inputs and compares reach gates. The native tape's distance
gaps were zero, but the residual mismatch cause was not established; neither
height-cache nor scheduling explanations should be claimed without evidence.
`player/player_active_touch_model.hpp` is a provisional
`(P, J) -> (Δv, Δω)` model: reach/height/surface gates before any projection,
`J = mass*(target - endpoint_velocity)`, contact normal along `J` so a centre
strike is torque-free, technique lateral/vertical offsets for side/top spin via
`ApplyImpulseAtPoint`, and no angular-velocity assignment. Tests cover the
requested spin cases, left/right mirror, moving versus static strikes and Magnus
stability over 0.5/1/2 s. Production still uses the legacy touch path.

P5c-3 historically added an immediate opt-in `ApplyBallImpulse` /
`Ball::ApplyContactImpulse` path. That path is now deleted, not an additional
production authority. `EnableActiveImpulseProduction` selects the prepared
tick path below. `DecideContactAuthority` gives genuine passive impacts priority
over any active action; overlap-only correction does not suppress a strike.
Production defaults remain OFF. Prepared strikes currently use zero-offset
point impulses, not the historical provisional technique table or a final
calibrated foot geometry.

P4d-2 adds the opt-in passive body production switch (default OFF).
`Simulation::EnableBodyPhysicsProduction` feeds the P4b/P4d-1 predicted upright
collider list into the single authoritative `Ball::Step(BallTickInput)` and skips
the legacy `ResolveBallPlayerContacts` block. With active preparation OFF,
fused legacy active touches still mutate the Ball after Step. The passive-only
the ball. The production collider list is kept separate from the low-pose
proposal buffer so an uncalibrated sliding/trip proposal cannot leak into
production. Tests assert, for 4000 steps in both processing orders, that the
production ball state exactly equals the unified kernel result and that the tape
contains real passive contacts. OFF remains byte-identical to the frozen
fingerprint; the switch stays OFF until the low-pose calibration and
ghost-blocking tests are complete.

P6b now has experimental rule production on the prepared path (below), using
AcceptedTouchSink as its submission port. P7 promotion remains blocked: legacy
Ball compatibility APIs still have default callers, and pose/technique/rule
acceptance and a reviewed new Golden are not complete.

### P5e prepared tick execution (experimental, default OFF)

`simulation_prepared_tick.cpp` provides the genuine prepare/arbitrate/Step/commit
path behind the independent internal `EnablePreparedTickProduction` gate.
Player::PrepareTick advances animation/locomotion once; CommitTick applies fatigue
and send-offs after accepted touch consequences. The Player's proposals go through
`player_touch_preparation.hpp`, not the Shadow observer. All actors read the
same passive endpoint and prior authoritative touch history; proposals are not
synchronously accepted. Actors get a const Ball reference and no mutable legacy
Ball port; the passive endpoint view is also checked for mutation after each actor.
Ball::Predict(BallTickInput) previews the same kernel as Step without consuming
forces. The final Step accepts at most one active impulse OR a kinematic endpoint
constraint for retention, with notifications only after commit. Legacy fused
execution remains the default; the earlier immediate impulse API is removed.
Current-Tick winner/buffer clear before all normal/ceremonial/terminal exits.
The diagnostic observer is optional, generation-guarded and cannot silently
discard candidates at a vector capacity limit. Actual committed endpoint
evidence includes the applied impulse or endpoint constraint. This is not P7
promotion. See `the removed design notes` for the timing change and
remaining acceptance gates. CTest wall time is NOT a 10ms tick benchmark.

### Reviewed rule touches on the prepared path (P6b experimental)

`simulation_rule_touch.cpp` produces `event/RuleTouch` with Tick, player/part,
source (prepared action / CCD / retain acquisition), step/generation and physical
contact provenance. It publishes through the existing AcceptedTouchSink AFTER
Ball commit. Mapped active-player approaching impacts are accidental touches only
on the first accepted impact in a geometric episode. Corrections cannot arm
suppression; geometric separation, resets and end changes re-arm it. This policy
is experimental because the upright collider geometry is not final calibration.
CCD opponent deflections preserve prior offside positions; the referee still
judges an offside attacker receiving the ball. Acquisition is recorded once;
retain anchoring is not a repeated rule touch. Default legacy producers preserve
their previous semantics. Native ON/ON prepared runs prove both actual accepted
CCD contacts and same-Step active strikes exist in both processing orders; this
does not replace complete-match/last-touch/restart acceptance.

### Baked pose geometry (P4f foundation, NOT calibration acceptance)

`player_baked_body_pose.hpp` evaluates local baked rotations through the explicit
bind hierarchy from `data/media/objects/players/player.object`; no runtime asset
parser or offline dependency is introduced. Only the baked root Z is applied:
PlayerState already contains root XY. Non-root keyframe positions are ignored,
matching offline Animation::Apply. The animation base angle is explicit, not
inferred from already animated bodyFacing. Pose-derived torso/head and four leg
capsules expose real Sliding/Trip shape phases. Tests cover hierarchy/root-motion
deduplication, mirror transforms and every baked Sliding/Trip frame.
These are NOT yet final production ColliderMotion inputs. P4f-1 now supports
endpoint-linear rotating/stretching capsules and local surface velocity (below).
Actual actor-pose/action-switch continuity, technique/Magnus calibration, rule
acceptance and P7 default/Golden/API removal remain pending.

### Prepared pipeline Shadow observation

When enabled, the active Shadow now observes the actual cached preparation
model proposals, rejects and single committed winner. Animation/frame provenance
is captured during the Player's one real pass. `prepared_impulse` identifies the
actual model point/impulse; the old desired-center surface proxy is used only
for legacy observations and is labelled as a diagnostic, not calibration.
Losing proposals remain NoImpulse and are never counted as executed strikes.
A both-order ON/ON replay test compares Shadow enabled/disabled physics, actor
frames/positions and RNG, and requires each reported executed impulse and point
to equal the same Tick's committed Step input. The diagnostic is optional and
does not run another Player action, RNG draw or Ball Step.

Acceptance evidence and explicit remaining blockers are recorded in
`the removed design notes`. P4f/P7 are not completed by these tests.

The switches are diagnostic experiment controls, not interchangeable production
engines: prepared active ON with body physics OFF previews static-only physics
and skips the fused legacy passive resolver. Only ON/ON exercises unified active
and player-body passive physics together. The four-switch replay is therefore
not proof that each OFF fallback preserves the other switch's legacy semantics,
nor permission to disable collisions for ON/ON acceptance.

### P4f-1 — endpoint-linear capsule CCD and anatomical IDs

Maintainer review of `e4298fb` accepted BallState/core physics and P5e's execution
boundary. Do not re-split the Player's process or redesign Ball's member API. Current roadmap
is P4f → P6c → P7; retain AcceptedTouchSink as the rule submission port.

`src/foundation/collision/sweep_deforming_capsule.{hpp,cpp}` extends the sweep
capsule endpoint displacements and changing radii. A conservative swept AABB,
quartic interior equation, cap quadratics and deterministic derivative-root
partition/bisection find the first incoming finite-feature contact without a
temporal sampling grid. Endpoint/radius interpolation is explicitly LINEAR,
not rigid SLERP or an inferred full rotation. Material velocity uses endpoint
blending plus minimal-twist angular surface transport and radius dilation.
`BallContact::surface_velocity` is consumed by the existing impulse/friction
response and mirrored by both Simulation result adapters. No extra normal
Step, actor execution, RNG draw or direct spin assignment is introduced.

Six Baked segment identities occupy the separate `0xc0000000` band; the coarse
three-part band is constrained below it without changing normal existing IDs.
`BuildBakedBodyColliderMotions` takes both actual geometry endpoints and a fixed
roster slot, with anatomical part mapping independent of physical identity.
Tests cover thin fast mid-Tick contacts, degeneracy, exit/separating rejection,
local velocity/response, deterministic ordering/frame transforms, an independent
distance oracle, zero-allocation queries, ID boundaries and every real adjacent
Sliding/Trip pose pair. This is not P4f-2 provider/clip-switch calibration or
production admission. Definitions/validation: `the removed design notes`.

### P4f-2 runtime provider experiment (not production acceptance)

`player_baked_pose_sample.hpp` observes actual runtime clip/frame/base yaw/scale
without executing actors or RNG. `player_baked_pose_realization.hpp` declares
shared skeletal entry blending/rotation and root-Z smoothing, adapted from the
offline simple Animation::Apply branch. Raw targets are not continuous poses.
`simulation_baked_body.cpp` builds a separately owned six-ID tape from realized
adjacent physical poses; coarse Shadow buffers cannot overwrite it. The internal
Baked gate requires prepared+body ON, defaults OFF, and is invalidated by Stop
or prerequisite disable. Reset/end changes reseed rather than sweep placements.

The first-roster pose source is exposed through CommittedBakedPoses at physical
commitment, not post-rule placement. Actors read a shared const forecast; actual
single-pass preparation endpoints refresh const arbitration evidence before the
one Step. No action/RNG repetition or extra normal Step is added. Rule episodes
group adjacent leg surfaces while retaining distinct physical provenance.

Named head/thigh/shin mesh envelopes are calibrated offline and tested against
immutable vertices, without runtime Mesh dependency. Torso, boots, hands, running
gait and sub-Tick render interpolation remain further gates. Correction after
eb2ffe8: baked action_type is e_DefString, not e_FunctionType. The previous
claim of no Movement clips was wrong; decode with StringToFunctionType.
Non-injected 24-second provider replay had zero body impacts;
a separately labelled constructed head strike verifies six-ID CCD RuleTouch.

Tests: player_baked_pose_runtime_test.cpp, player_baked_pose_realization_test.cpp,
player_baked_mesh_envelope_test.cpp and player_baked_body_pipeline_test.cpp.
Contract and evidence: the removed design notes. P4f-2 acceptance,
P4f-3 technique calibration, P6c semantic/full-match acceptance and P7 reviewed
Golden/default/Legacy removal are not complete. Assets/Golden remain unchanged.

P4f-2b diagnostics correct the asset-action type confusion without changing asset
bytes or runtime selection. Actual native running poses advance thigh rotations
in both processing orders. Dense 1/16-Tick samples cover 28,288 adjacent intervals
across 1,016 Movement, 12 Sliding and 38 Trip clips; maximum primitive endpoint
deviation versus local-quaternion interpolation is 8.856 mm raw / 14.069 mm
realized. This is a measured error, not yet a production admission threshold.
The 0.215 m realized native maximum is same Movement clip 1147, frame 7->8,
NOT a clip switch frame. Root translation, base yaw and skeletal local motion
were separated: the raw clip 1147 frame 7->8 ankle moves only ~0.044-0.075 m
(4.4-7.5 m/s honest sprint gait), while the realized peak is ~0.215 m
(~21.5 m/s). The realized pose is the result of entry-smoothing catch-up after
the preceding clip switch (clip 1156 -> 1147, with a base-angle/rotation-
smuggle reset): the reduced entry rate holds the realized pose behind the raw
target and the inherited 7.5*pi rad/s per-joint rate limit then releases it as
a bounded, transient surface-speed spike. The entry slerp bias toward the
previous realized pose was removed (one lag source); the rate ramp and window
remain as validated (a 16-frame window measurably worsens the peak). This is a
diagnosed bound, not an approved production tolerance; a full action-aware rate
redesign is deferred because it would also tighten legitimate kick headroom.
Prior asset Trip/Movement evidence based on direct e_FunctionType comparisons
is superseded.

### P4f-2b additional contact regions (experimental checkpoint)

`player/player_baked_contact_regions.hpp` owns nine Capsule/Sphere regions for
chest, boots, arms and distal palms. The pelvis primitive now follows body,
and trunk follows middle separately. Six primary IDs are preserved; extra
fixed-slot IDs use checked 0x40000000..0x7fffffff. Simulation's dedicated tape
contains all 15 shapes from the shared committed skeleton, independent of Shadow.
Named local mesh vertex enclosures, two scales, mirror/ID limits and exact
actual provider tape/public-pose A/B replay are covered by the extended tests
and new unittests/player_baked_contact_regions_test.cpp. Runtime loads no meshes.

Dense 1/16-Tick interpolation now includes all 15 shapes: maximum deviation
0.0112886 m raw / 0.0186184 m realized. This supersedes the six-shape result;
production error acceptance and 0.215 m/Tick entry catch-up assessment are open.

Experimental authorized boot CCD kicks carry AcceptedTouch::physical_kick to
BallTouchFacts through AcceptedTouchSink, allowing Ready taker release from
physical evidence rather than marker timing. Non-taker/non-Ready contacts remain
unauthorized before live play; other live body contacts remain deflections.
Legacy producers default false. Intent-window and comprehensive rule acceptance
remain P6c work; this is not blanket acceptance of all physical body impacts.

Full evidence and explicit blockers: the removed design notes. Defaults,
Golden and assets remain unchanged. Stop after this checkpoint commit per the
maintainer; do not proceed to P4f-3/P6c/P7 or Ball Model Bake without resumption.
Development builds from now on require Debug/core only, with app disabled,
as specified in the Build and test section.

S0.3 (done) removes the last unconditional adoption. `Player::PrepareTick` no
longer adopts a second state after the animation pass: every path already
finished its own write inside that call (the pure-locomotion branch commits and
projects; each legacy root-motion branch adopts where it produced the position,
in `CalculateSpatialState` / `CalculateFactualSpatialState`). Re-adopting there
was identity at best and a hidden re-read at worst.

`Player::previousPosition2D` is documented as a legacy animation cache, not a
second world-position authority: it is written by the state projection and the
legacy reset/root-motion paths, and read in exactly one place
(`CalculateSpatialState` derives the legacy `actualMovement` from
`position - previousPosition2D` on the root-motion branch). The pure-locomotion
branch never reads it. Coverage: the 200-tick mirror-invariant case still passes
with the adoption removed, over both paths (156 legacy ticks and 44
pure-locomotion ticks, 11787 assertions).

S0.4 (done) finishes the lifecycle group. `Player::Mirror` now mirrors through
`CommitKinematicState` instead of writing the state directly, keeping the
historical asymmetry on purpose (`PlayerKinematicState::Mirror` mirrors position
and velocity only; facing and bodyFacing are left to the legacy convention).
`ResetPosition`, `ResetRuntimeState` and `Activate` keep adopting the action pass's
reset pose, but through the explicitly named `CommitLegacyActionMotion()`.

S0 is complete, and the write closure is verified by search:
`PlayerKinematicState kinematicState` is assigned in exactly one place
(`Player::CommitKinematicState`), and that function has exactly four callers:
`CommitLocomotion` (continuous motion), `CommitLegacyActionMotion` (legacy
animation root motion, six classified call sites: CalculateSpatialState,
CalculateFactualSpatialState, ApplySimulationMovementState's legacy branch,
ResetPosition, ResetRuntimeState, Activate), `ApplyPositionCorrection` (contact
and goalkeeper-boundary corrections) and `Mirror`. There is no other implicit
write path. Coverage: 17 runtime cases / 11808 assertions, including a 200-tick
mirror invariant over both paths.

S1.1 (done) gives the capability in force exactly one source:
`Player::EffectiveMotionProfile()` = the definition-derived profile
(`MakePlayerMotionProfile`) with the established `GetMaxVelocity()` top speed.
`CommitLocomotion` reads it instead of assembling the profile itself, so
execution and (next) prediction have one place to read. Pinned decision, with a
test: the runtime modifiers scale *abilities* (`GetStat`) today but do NOT scale
the motion profile, so `max_speed` keeps its formula with no fatigue or
AI-difficulty factor. Whether fatigue should slow a player down is a separate
behaviour change.

### ANIM-1 A6.3-A9 completion

The remaining stages closed the interface, and their step-by-step detail is in
git history rather than here:

- `Player::TrySelectAction` owns the selection entry point,
  `Player::InstallSelectedAction` the installation, `Player::Process` the
  dispatch; the per-tick action clock is authoritative in `PlayerActionState`.
- The passive trip decision and its local queue, the action displacement/body
  state, the movement smuggle and the goalkeeper boundary clamps are
  Player-owned.
  `spatial_state_` hold the values (`Anim`, `SpatialState`), and the remaining
  `currentAnim`/`spatialState` mentions are parameter or comment names, not a
  second state.
- `SelectAnim` and its whole chain, `Humanoid`/`HumanoidBase` and the
  compatibility cursor writes were deleted in commit `60c7378`; `905c808`
  removed the leftovers.

### ANIM-2 A11: Animation playback ownership, one Player input, forecast cleanup

A11 froze the boundary between `Player`, `Animation` and body physics; A11.5/A11.6
closed the leftovers that still confused "prediction" with "playback".

- **A11.0-A11.4 (already landed):** `Player::UpdatePlayback` owns the animation
  instance (`PlaybackState`) and the final pose; `Animation` exposes the stateless
  `Match`/`MatchTouch` plus the pure `PlaybackState`/`AnimationTransitionState`;
  `Match` consumes only what it needs; the commit decision is a named Player stage
  (`CanCommitMatchedSelection`); `football::animation` is its own runtime archive
  over a shared format target.
- **A11.5 body forecast is named for what it is.** `Player::PreviewPose` became
  `Player::PreviewRawPose`: it returns the RAW clip skeleton, not the final blended
  playback pose, so a forecast and the actual endpoint may differ mid-transition.
  The requested frame is clamped to the clip range in BOTH directions, so no offset
  can index out of bounds.
- **A11.5 one pending-input boundary.** `Player::React` is the single passive-
  reaction entry; contact sites no longer call `React` and `TripMe` separately.
  `React` arbitrates, records the reaction and stages the ANIM-1 A7 trip guard
  (private `StagePassiveTrip`). The next `RunActionTick` consumes the reaction and
  applies the Trip interrupt, exactly where the legacy direct write took effect.
  The physical collision impulse is still applied immediately by the contact phase;
  only the ACTION reaction waits for animation. An accepted `Player::Play` request
  is translated at the same boundary into the controller command queue
  (`BuildCommandQueueForRequest` -> `BuildPlayerCommands`), so active and passive
  inputs run through one selection/install/execution pipeline. A deferred request is
  not retained as an execution input; the producer resubmits it. The counters now
  say what they mean: `ActionRequestInputsConsumed()` (a request produced a
  non-empty command queue; NOT that a clip installed) and
  `ReactionTripInterruptsScheduled()` (a Trip interrupt was scheduled; NOT that
  the trip clip installed), plus `ActionRequestPreemptions()`.
- **A11.6 `MatchTouch` diagnostics are opt-in.** `MatchTouch` returns
  `TouchMatchResult` and takes an optional `TouchMatchTrace*` (default `nullptr`).
  Production passes nothing and records no per-candidate vector; the result is
  identical either way.
- **Tests.** The runtime preview case checks both clamp directions and realizes a
  finite body collider; the CCD consistency suite adds a mid-blend interruption and
  checks the realized collider geometry, not only the raw pose. New runtime cases
  cover `React`-driven trips and an accepted `Play` request executing through the
  pipeline.
- **A11.7 input lifetime.** Recording a reaction is atomic with the trip
  staging (`StagePassiveTrip` returns whether it staged): a second trip that
  loses the guard keeps the first accepted reaction, and a non-executable
  reaction (Bump) never clobbers a staged trip. A Trip that executes discards
  the accepted active request (`ActionRequestPreemptions()`), so a stale
  `Play(Shot)` cannot switch the actor back out of the trip on the next tick.
  `Play(ActionRequestKind::Move)` is REJECTED: continuous movement intent is
  `SetControl`/`PlayerMotion`'s, and a Movement clip from `Play` would not change
  it. `PlayerBakedPoseSample` frame fields are 64-bit and `PreviewRawPose` adds
  the offset in 64 bits before clamping, so extreme offsets cannot overflow. A
  reused `TouchMatchTrace` is reset (capacity kept) at every `MatchTouch` entry.
- **A11.7 tests.** Same-tick trip/trip/bump atomicity and the `Play(Move)`
  rejection run in the existing per-player contract case; a new runtime case
  proves a Trip preempts an accepted `Play(Shot)` with no later Shot; the preview
  case exercises both clamp directions and `INT_MAX`/`INT_MIN`; the MatchTouch
  equivalence test now uses a SUCCESSFUL selection and checks trace reuse.
The frozen ownership: AI/Referee/Physics -> `Player` (ActionState time authority,
PlayerMotion capability, Animation Match/Playback) -> final pose -> body
realization -> physics collider. `anim_state_` still carries install staging,
smuggle and reference data; it is removed incrementally, never in one destructive
sweep. `MatchTouch` shadow diagnostics and `BakedBodyPreviewChecks` remain
observation-only.

### ANIM-2 A12: two Animation archives, algorithms out of the headers

A12 is a compile/link organization pass, not a behaviour change: the public
types and declarations move to the headers, the algorithms move to
`src/animation/*.cpp`, and the module splits into two archives. No clip field
layout, `.simanim` byte format, `AnimationId`/namespace naming, Match/MatchTouch
scoring, Player `anim_state_`, arbitrary-frame matching or `.so` conversion was
touched.

- **A12.0** `football::animation_base` (STATIC: `format.cpp`, `clip.cpp`,
  `library.cpp`) + `football::animation` (STATIC) on top. The baker links only
  the base. `format.hpp`/`clip.hpp`/`library.hpp` keep the types and the
  `Serialize`/`Deserialize`/`Load` declarations; the byte-level bodies and the
  FNV-1a reader hash moved out. Verified: the offline bake is byte-identical
  (same sha256 as before).
- **A12.1** `selector.cpp` (`CrudeSelection`, `GetQuadrant`) moves into
  `animation_base`; `CheckFunctionType` and the deviation constants become
  file-local. Verified: bake byte-identical and `--verify-selection` passes
  (187 queries, exact vectors).
- **A12.2** `movement_selection.cpp` (`RefineMovementCandidates`,
  `RefineNonMovementCandidates`, `RefineLegacyCandidates`,
  `SelectIdleMovementAnim`, `SelectRetainAnimClip`) into `football::animation`;
  the stable sorts, direction/body filters, similarity ratings and rating scratch
  live in the .cpp anonymous namespace. The header keeps the request struct, the
  config-vector accessors and the `ForceInto*` snap helpers the callers/tests
  use.
- **A12.3** `touch_geometry.cpp` and `touch_scoring.cpp` hold
  `EvaluateTouchGeometry`/`SegmentProjectionParameter`/`FrontOfFootOffsetRel`
  and `ResolveActionSmuggle`/`EvaluateBodyBallDistanceAdvantage`.
- **A12.4** `playback.cpp`, `transition.cpp`, `pose_blend.cpp`,
  `pose_sampling.cpp` and `motion_clip.cpp` hold the state advances, sampling
  and reference-motion sampling; the headers keep the value types.
- **A12.5** header includes slimmed to what each header actually uses
  (`movement_selection.hpp` no longer pulls `selector.hpp`; `playback.hpp` no
  longer pulls `pose_sampling.hpp`; `transition.hpp` includes `clip.hpp` not
  `pose_blend.hpp`); both target declarations live in `src/animation/CMakeLists.txt`.

Verification for A12: the offline bake stays byte-identical and
`--verify-selection` stays exact; Debug `ctest` is 10/10; and because moving
floating-point code across translation units can change inlining/contraction,
the Release build is checked for the same unit/loco results.

### ANIM-2 A13: arbitrary legal-frame entry matching

A13 lets a clip be entered at a legal frame whose pose matches the actor's
current final pose instead of always restarting at frame 0. The Animation
boundary is unchanged: Animation selects only; the Player still owns the
action clock, the world motion and the install.

- `entry_frame.{hpp,cpp}` (football::animation): `PoseDistance` (summed
  shortest-path joint rotation difference plus a weighted joint position
  difference) and `SelectEntryFrame`, which returns the best frame in a legal
  window (`min_remaining_frames`). No reference pose, an empty clip or no legal
  frame keeps frame 0. Pure: no Player/Ball/Simulation/RNG.
- `MatchQuery.entry` (`MatchEntryRequest`) and `MatchResult.start_frame`: `Match`
  fills the entry frame for the front candidate when the caller enables it.
- Player (A13.1): Movement and Trip install at the matched frame. The
  locomotion adaptation can reorder the Movement candidates after `Match`, so
  the final Movement pick is Animation's (A13.3): the Player resolves the
  locomotion preference (it needs PlayerMotion's snapshot) and passes it in
  `MatchQuery.preferred_movement_clip`; `Match` applies `ResolveMovementPick` and
  reorders the candidates, so `MatchResult.clip_id`/`start_frame` are final and
  the Player only accepts/rejects and installs. `ProjectSelectedActionCompat`
  sets `anim_state_.frameNum` to the entry frame and re-bases
  `anim_state_.positions` by the entry frame's reference position, so the first
  committed tick adds only the per-frame root delta, never the skipped
  displacement. Deltas, outgoing movement and smuggles are unchanged by that
  constant shift. Touch actions stay at frame 0 until A16.
- Tests: `entry_frame_test` (distance, legal window, frame-0 fallback, Match
  wiring), a `match_test` case proving the preference wins and the entry frame is
  matched against the final clip, and a runtime test where a turning Movement
  tape installs a non-zero entry frame, the following tick moves ~0.06 m (no
  teleport), and install/cursor mismatches stay 0.

A14 (Trip/Bump clip choice from collision direction/pose) and A16 (non-zero

### ANIM-2 A14: Trip clip + entry-frame matching

A14 gives a Trip its own match instead of the generic legacy refinement.
`trip_match.{hpp,cpp}` (football::animation) scores each Trip candidate by:

  2.0 * (1 - dot(clip fall direction, relative impact direction))
  + 1.0 * |RangeVelocity(clip.incoming_velocity) - |current velocity|| / sprintVelocity
  + 0.5 * (best legal entry frame's PoseDistance / kBodyPartCount)

The fall direction is the clip's `bump_direction`, else its
`incoming_body_direction`. The entry frame comes from the A13
`SelectEntryFrame`, so the same pose-continuity primitive is reused. Pure: the
Player supplies the impact direction, current velocity and pose; Physics keeps
the impulse.

- `MatchBranch::Trip` runs `SelectTripMatch`, puts the winner in front and
  returns its `(clip_id, start_frame)`; the Player sets the branch and inputs for
  `e_FunctionType_Trip` and no longer runs the legacy foot/perturbation block for
  Trip. Everything else (arbitration, install, physics) stays Player-owned.
- Tests: `trip_match_test` (determinism, trip-type filter, direction
  preference, pose-matched legal entry frame); the runtime trip chain still
  yields Movement -> Trip -> Movement with install/cursor consistency.


### ANIM-2 A15: switch continuity at a matched entry frame

A15 adds the continuity evidence for non-zero entry frames (A13) and Trip
matching (A14); no production algorithm changed.

- `entry_frame_continuity_test` (pure): a matched entry frame blends the first
  tick from a pose that is already close, so the first-tick pose jump is smaller
  than a naive frame-0 restart and stays below 0.01 rad on the synthetic clip.
- Runtime: a turning Movement tape measures, per tick, the max joint rotation
  change (neighbourhood-aligned, so a quaternion sign flip is not counted) and
  the body-collider center change relative to the whole-body displacement (so a
  collision push is not mistaken for a pose break). Bounds: jump < 0.8 rad,
  collider < 0.8 m; measured 0.53 rad / 0.54 m. Install/cursor mismatches stay 0.


### ANIM-2 A16: entry frames for touch actions

A16 extends the matched entry frame to the touch actions (BallControl, Trap,
Interfere, Deflect, Short/Long/High pass, Shot) under one hard rule: the entry
frame is always strictly before the touch frame, so a contact is never skipped.
The Player recomputes the frame with `EntryFrameRequest.last_legal_frame =
touch_frame - 1` instead of reusing Match's answer.

The reference-motion re-base (A13.1) is invariant for the touch math: the trap/
ball-control vectors use the clip's `root_positions` at the absolute frame and
the `outgoingMovement` difference, both unaffected by the constant shift. The
scheduled contact stays the clip's absolute `touch_frame`, and the action
cursor starts at the entry frame, so `IsContactPending()` is simply
`entry_frame < touch_frame`.

Tests: `entry_frame_test` proves the touch bound wins over a late pose match;
the runtime pass test asserts the pass's entry frame is before its contact
frame and the installed touch frame equals the action state's contact frame.


### ANIM-2 A17: bounded full-roster acceptance

A17 is the closing acceptance for the A13-A16 entry-frame work. It does not run
a full match (the repo rule):

- A bounded 11v11 runtime case builds 22 actors over one Simulation-owned
  `AnimationLibrary`, drives two actors through pass/dribble/shot/trap for 220
  ticks (24 -> 18 clip switches observed), and hashes every actor's clip id,
  frame, action type and position plus the ball position each tick. Two
  independent runs must produce the same FNV-1a digest; that pins 22-actor
  library sharing and end-to-end determinism after A13-A16.
- The runtime CTest timeout is raised to 420 s: the bounded suite is now ~4 min
  on the reference host. The timeout still only catches a hung Step.

A17 closes the entry-frame program: Movement/Trip/touch actions all select
their clip and legal entry frame in Animation, while the Player still owns

### ANIM-2 A18: entry-frame bounds and touch time/space consistency

A18 fixes the review findings on top of A13-A17; it does not change any other
action path.

- EntryFrame bounds (P0): `last_legal_frame` now uses a real sentinel,
  `kEntryFrameUnbounded`. The caller's bound is ALWAYS intersected with the
  duration bound (`frame_count - 1 - min_remaining_frames`), so an oversized
  explicit bound can never index past the clip, and a negative explicit bound
  (for example a touch frame of 0) means "no legal entry frame" rather than
  "unspecified".
- Touch time/space basis (P0): `MatchTouch` now selects the entry frame inside
  the match (`TouchMatchContext.reference_pose`/`has_reference_pose`), then uses
  `contact_delay_ticks = touch_frame - entry_frame` for the ball prediction, the
  out-of-bounds check and the last-touch bias, and evaluates the tap geometry
  against the reference position relative to the entry frame. `TouchMatchResult`
  and `TouchChoice` carry the matched `start_frame`; the Player installs exactly
  that and no longer re-selects after `MatchTouch`. `entry_frame == 0` keeps the
  legacy basis bit-for-bit. A `kTouchPrepFrames` window keeps a few frames before
  the touch so the smuggle has a sane spread.
- Action Smuggle execution (P0): `ActionSmuggleTickInput` gains `start_frame`,
  and `ActionSmuggleTick` expresses the action/movement/rotation smuggle on the
  frames that actually remain. With `start_frame == 0` the relative values equal
  the absolute ones and every floating-point result is the legacy one; with a
  non-zero entry the whole target is delivered by the touch frame (previously the
  skipped head of the window left a large shortfall).
- Trip entry window (P1): `SelectTripMatch` requires the entry frame to leave
  at least half the clip (`kTripMinRemainingFraction`), so pose similarity cannot
  pick a late recovery frame; the direction/velocity/pose weights are unchanged.
- Test hardening (P1): the A17 digest also hashes every actor's 14-joint
  `PlaybackPose()` and the accepted-touch facts. `touch_match_test` forces a
  NON-ZERO entry with a non-constant trajectory and checks the ball position at
  `(touch - entry) * 10` ms; `player_action_motion_test` proves the non-zero
  entry accumulates the full action smuggle and that `start_frame == 0` is the
  legacy formula bit-for-bit; `trip_match_test` uses a late reference pose to
  trigger the window with pose matching enabled; a runtime case drives a close
  ball into a Trap and asserts entry < contact and a touch fires.

## P19: Player boundary consolidation

P19 tightens the Player boundary without rewriting the Player: the same
authoritative state (Kinematic/Action/Playback), the same Animation/Motion/
Ball/Rules algorithms and the same tick order, RNG draws and touch publication.
Every stage was committed on its own and verified against the A17 digest
`5bef607ddd567a09`, `A6.3a clip_match=68 clip_mismatch=1 fallback=1`,
`install 11/11/0`, `authority 42/42/0`, `A7 trip 206/265` and
`nm pure_ticks=4 touch_selections=4`. Debug and Release both stay 10/10 CTest.

### P19.0: method ownership audit

Audit baseline `c1b1344`, HEAD and worktree re-checked. Each public method was
matched against its real callers (not its name or its file), and the outcome
decided visibility:

| method | real callers | visibility after P19.3 |
| --- | --- | --- |
| `Process`, `PrepareTick`, `CommitTick` | Simulation / prepared tick | public (Runtime API) |
| `Play`, `React`, `Phases` accessors | Runtime tests, contact code | public |
| `Activate`, `Deactivate`, `Exit`, `Mirror`, `ResetPosition`, `OffsetPosition` | Simulation, Referee | public |
| `SetControl`, `ClearControl`, `SetTeamContext`, `SetFormationEntry` | Simulation | public |
| `UpdatePossessionStats` | `runtime/team_possession.cpp` | public |
| `CommitLocomotion` | runtime harness (direct motion test) | public |
| `SelectRetainAnim`, `SendOff` | Referee, test access | public |
| `GetReactionTime_ms`, `GetControlSpeed` | Player touch math, Simulation shadow | public |
| `GetCurrentAnim` | Simulation snapshot | removed -> `GetCurrentAnimationId` |
| diagnostics counters | tests | public (explicit diagnostics) |
| `TrySelectAction`, `SelectBestTouchAnim`, `SelectTouchAction` | Player selection only | **private** |
| `BuildCrudeSelectionQuery`, `NeedTouch`, `CanCommitMatchedSelection` | selection only | **private** |
| `InstallSelectedAction`, `CommitSelectedAction`, `BeginSimulationAction` | Player install only | **private** |
| `BuildTripSelectionCommands`, `ComputeAndCommitActionMotion` | action tick only | **private** |
| `ComputeMovementSmuggle`, `ApplyFactualSpatialState`, `ApplyGoalkeeperBoundaryClamps` | action tick only | **private** |
| `StepSimulationAction`, `AdvanceActionTick`, `RequestCommand`, `UpdatePlayback` | Player clock/decisions only | **private** |

The live `PlayerTickContext` references (`play_authorized`, `set_piece_active`,
`ball_in_play`, `half_underway`) were left as references on purpose: the
synchronous touch notification can move competition state inside the same
`Process()` call, so a tick-start snapshot would be wrong.

### P19.1: team state has one writer

`PlayerTeamView` is now a read-only borrow (`const TeamMatchState*`) and its
`SetDesignatedPlayer()` is gone. `PlayerRuntimeSink` gained
`SetDesignatedPlayer(int team_id, Player* receiver)`; Player passes through it,
`Simulation::RuleCommands` and the prepared-tick `PreparationPorts` implement
it, and Runtime remains the only writer of `team_states_`. The designation
time is unchanged: still before the physical touch is committed, synchronous
and not publishing team facts. A runtime case proves same-tick visibility and
that the explicit team id keeps the write on the actor's own team.

### P19.2: rules authorize, the builder translates

`src/sim/referee/action_authorization.{hpp,cpp}` owns
`EvaluateActionAuthorization(PlayerActionAuthorizationInput)` (pure) and
`IsGoalkeeperBackpass`. `BuildPlayerCommands` no longer interprets
`RestartPhase` or computes keeper-hands legality; it samples the live facts at
the point the original checks ran and consumes the result. The permissions stay
separate: a pending restart keeps movement but forbids the action, a non-taker
must also stand still, and keeper hands are a separate capability. A pure test
pins each branch. Same command queue contents and order.

### P19.3: the interface is closed

Sixteen internal execution entry points moved to `private` (see P19.0) and
`GetCurrentAnim()` (which exposed the legacy `Anim` storage) became the
read-only `GetCurrentAnimationId()` used by the runtime snapshot. No state
layout, timing or behavior changed.

### P19.4: player reachability is a pure value

`ComputePlayerReachability(PlayerReachabilityInput)` in
`src/sim/player/reachability.{hpp,cpp}` is a pure value over the actor's
kinematic state, action state and ball, returning
`PlayerReachabilityFacts {usual_ms, optimistic_ms, locally_controllable}`.
`UpdatePossessionStats` consumes it and keeps the cross-team comparison
Runtime-side; `hasBestPossession`/`hasUniquePossession` stay as compatibility
projections. `ComputeHasPossession` and the exact-rollout horizon moved with
it. This is the value a future Player AI can read as an Observation without
calling `Player::Process`.

### P19.5: compatibility state and diagnostics audit

Read/write inventory of `anim_state_`, `spatial_state_`, `startPos`,
`nextStartPos`, `previousPosition2D`, `interruptAnim`, `reQueueDelayFrames`:
every writer is a Player member (reset, install, projection, clock advance,
smuggle evolution); no Simulation, Rules or contact code writes them, so there
is no cross-module multi-writer to remove. `player_diagnostics.hpp` now records
the storage scope of its telemetry: every `int&` counter is process-global
aggregation, the Player fields are single-owner, and the shadow reports are
Simulation-owned. Promoting the global counters to a Simulation-scoped census
is intentionally deferred; it is not needed for correctness today and must not
change RNG or tick order.

### P19.6: acceptance

All changed code compiles into `football_sim_domain`; no new target, no
`.a`/`.so` split and no Team domain were added. `ctest --preset debug` and
`ctest --preset release` are 10/10 with the unchanged A17 digest, and the
runtime suite now carries three extra cases (port write, action
authorization, reachability). A18.4's non-zero entry-frame Smuggle regression
is unchanged.

## S20: physical directory and header visibility

S20 is a pure move: no namespace, CMake target name, link edge, tick order or
RNG change. The A17 digest `5bef607ddd567a09` is identical before and after in
both Debug and Release, and all ten CTest suites pass in both.

### S20.0: public value types move to include/football/

The sim_contracts value types now live in the top-level include tree and
`football_sim_contracts` exports `${PROJECT_SOURCE_DIR}/include` instead of
`src/`:

```
include/football/player/{player_definition,player_control,player_control_set,
                         player_role,formation_entry}.hpp
include/football/referee/game_mode.hpp
include/football/team/{team,lineup,leadership,set_piece_orders,selection,
                       formation,formation_preset}.hpp
include/football/runtime/{active_lineup,match_config,phase,result,simulation_config}.hpp
include/football/observation/{world_state,snapshot}.hpp
include/football/event/{event,event_trajectory}.hpp
```

`football/team/formation.hpp` carries the static formation value types;
`BuildFormation` stays internal in `runtime/formation_build.hpp` because it
creates Player FormationEntry values, and consumes `team::TeamDefinition`.
`snapshot_history.hpp`, `temporal_graph.hpp`, `player_slots.hpp` and
`foul_assessment.hpp` are internal-only (each has a single implementation
consumer), so they stopped being exported instead of moving into include/.
`simulation.hpp` is now internal too.

### S20.1: module directories out of src/sim/

`src/sim/{player,referee,event,observation}/` became
`src/{player,referee,event,observation}/`. The internal include root is still
`src/`, so internal headers are reached as `player/...`, `referee/...`,
`event/...`, `observation/...`, while the public value types keep their
`football/...` paths. Each module owns a `CMakeLists.txt` that only declares
its sources with `target_sources(football_sim_domain PRIVATE ...)`;
`football_sim_domain`/`football_sim_contracts` are still declared by the
Runtime layer, and the root adds the module subdirectories after it.

### S20.2: Simulation merges into src/runtime/, src/sim deleted

The composition root, the `Simulation` implementation split and the runtime
helpers (`clock`, `roster`, `possession`, team state/views) now live together
in `src/runtime/`. `simulation.hpp` is an internal header: `football_sim_domain`
no longer exports any header file, and the shared `src/` root is a PRIVATE
implementation include for the domain target, for libsimcore (`env.cpp`
includes `runtime/simulation.hpp`) and for the internal test harnesses. The
repository no longer contains `src/sim/` and no source uses `#include "sim/..."`.

### S20.3: CMake, smoke test and acceptance

- `unittests/public_header/` builds an OBJECT library that includes only
  `football/env.hpp` and touches the public value types. It links
  `football::sim_contracts`, `football::ai_contracts` and `football::ball`
  only, so a public header that reached back into `src/` would fail the build.
- `unittests/runtime` and `unittests/locomotion` declare the `src/` root
  explicitly, since it is no longer propagated through `football::sim`.

Structural acceptance:

1. `src/sim/` does not exist and no source contains `#include "sim/..."`.
2. `include/football/env.hpp` compiles with `-Iinclude` alone (and the smoke
   OBJECT target builds), so the public contract has no `src/` dependency.
3. `football::ai` still links only the public Observation/Control contracts;
   `football::ball` and `football::animation` stay independently buildable.

Behaviour acceptance: Debug 10/10, Release 10/10, Core-only
(`BUILD_TESTING=OFF`) builds `libsimcore.so` and `football_sim`, and both
Debug and Release report the unchanged A17 digest `5bef607ddd567a09`.

## P21: Player AI protocol freeze and Default adapter

P21 freezes the AI <-> Player protocol and makes the policy replaceable without
touching Player, Animation or Physics. It does not change the 100 Hz tick.

### P21.0: Eliza audit

The P21.0 audit maps the historical `ElizaController`,
`TeamAIController` and off-the-ball strategies onto the public protocol, with
the keep-in-AI / keep-in-Player-Runtime / give-to-Rules split, the
function-level data flow, the already-extracted primitives
(`kick_targeting`, `reachability`, `perception`, `offside`), the Observation
gaps and the two known protocol gaps (single action vs ordered candidates, and
action feedback). Baseline frozen at A17 digest `5bef607ddd567a09`.

### P21.1: IPlayerPolicy, AIEngine, Default adapter

- `include/football/ai/player_policy.hpp` is the frozen protocol:
  `IPlayerPolicy::Update(const WorldState&, std::span<const TacticalBoard,2>,
  PlayerControlSet&)` plus `Reset()`, and `PlayerPolicyKind { Default, Eliza }`.
  The policy reads only public Observation and coach intent; it owns no actors,
  match state or RNG.
- `AIConfig` gains `player_policy`, default `PlayerPolicyKind::Default`.
- `src/ai/ai_engine.{hpp,cpp}` is the AI composition root. It owns the two
  persistent `TacticalBoard`s (the coach's intent) and the one active
  `IPlayerPolicy`, and `Update()` calls `policy_->Update(world, boards_, out)`.
  `Eliza` currently throws until P21.3 instead of silently running Default.
- The Default decision body moved to the internal `src/ai/default_decisions.*`
  and is shared by the public `DefaultAI` baseline and the internal
  `DefaultPolicy` adapter, so the two cannot diverge.
- `GameEnv` now holds `AIEngine`; the `Observe -> AI Update -> Simulation Step`
  order is unchanged.

Verification: `unittests/ai` compares `DefaultAI` and `AIEngine(Default)`
field by field (player, move_direction, desired_speed, look_at, action,
target_player, target_position, power) over open play, possession, a pending
restart, a set piece and an initial-tactics override, plus determinism and the
policy-kind selection. Debug and Release are 11/11 CTest, Core-only builds, the
public-header smoke still builds, and Debug and Release keep the A17 digest
`5bef607ddd567a09`. `src/ai` and `include/football/ai` contain no `runtime/`,
`player/`, `referee/`, `observation/`, `event/`, `testing/` or `sim/` include,
so the AI layer still depends only on the public value contracts.

### P21.2-P21.7: Eliza policy, switching and acceptance

- **P21.2 Observation.** `WorldState` gains per-player `role`, `fatigue_inv`,
  `time_to_ball_ms`, `short_pass_skill` and `workrate`; per-team
  `has_possession`, `fading_possession_amount` and `time_to_ball_ms`; and
  `ball_prediction` at 200/350/500/650 ms computed by the runtime Ball model.
  No `Player*`/`PlayerDefinition`/animation state is copied, and the new
  observation does not move the tick, RNG or physics (A17 digest unchanged).
- **P21.3 off-ball.** `src/ai/eliza/` ports the legacy off-the-ball role
  strategies, the support force field, `GetForceFieldMovement`, the defensive
  component and `GetDefendPosition` into pure functions over the observation.
  `ElizaPolicy` dispatches role-shaped movement; only the closest actor closes
  the ball, so the team keeps its shape.
- **P21.4 on-ball.** `on_ball.*` ports `GetOnTheBallCommands`,
  `_GetPassingOdds`, the short/long/high selection, the shoot decision (fixed
  odds threshold instead of the legacy RNG), `GetBestDribbleMovement`, the
  loose-ball Trap/Dribble gate and the Sliding gate. Output is a single
  `ControlAction` + target, still routed through `BuildPlayerCommands`.
- **P21.5 keeper and tactics.** `keeper.*` ports keeper positioning, rush and
  save intent; the save stays an intent because `BuildPlayerCommands` still
  rejects a backpass/out-of-box/retained save. Coach vs Player stays split:
  `MakeTacticalBoard` is the intent, the AI only interprets it. The positioning
  offside line is computed AI-side from the observation; the offside decision
  stays with Rules.
- **P21.6 frequency and request semantics.** `ElizaPolicy` owns a per-`PlayerId`
  possession clock (identity-keyed, `Reset()` clears it) that lowers the release
  threshold like the legacy `longPossessionFactor`. The P21 protocol
  freezes one `Update` per 100 Hz tick, a complete control set every tick,
  intents-without-ACK semantics, and the deferred future GNN submission path.
  The Player's own 20-240 ms decision clock is untouched.
- **P21.7 switching and acceptance.** `AIConfig::player_policy` selects Default
  or Eliza through the same `AIEngine`/`WorldState`/`PlayerControlSet`; both are
  deterministic, Default still matches the public `DefaultAI` baseline, and the
  two are genuinely different implementations. A runtime case runs the reverse
  team-processing order safely and deterministically.

Acceptance: Debug and Release are 11/11 CTest, Core-only (`BUILD_TESTING=OFF`)
builds `libsimcore.so` + `football_sim`, the public-header smoke builds, and
Debug and Release both keep the A17 digest `5bef607ddd567a09`. `src/ai` and
`include/football/ai` contain no internal `runtime/`, `player/`, `referee/`,
`observation/`, `event/`, `testing/` or `sim/` include, so the AI layer still
depends only on the public value contracts.

## TACT: FM-style dual-phase tactics, coach tools and heuristic Player AI

Implemented through TACT-8 in staged local commits; nothing here changes Physics,
the 100 Hz control protocol or the Default baseline.

### Value model and interpretation (TACT-1, TACT-2)

- `include/football/ai/tactical_plan.hpp` is the persistent dual-phase plan:
  canonical anchors (`u` own->opponent goal, `v` left/right), in/out-of-
  possession roles, team/transition/individual instructions and
  `ValidateTacticalPlan`. `MakeTacticalPlanV2` bootstraps it deterministically.
- `TacticalBoard` gained an explicit `BoardMode` and an `advanced` plan. Legacy
  behaviour (`width`/`depth`/`players`) is untouched; `ValidateTacticalBoard`
  couples the mode and the plan. Coach writes require V2.
- `tactical_interpreter.hpp` is a pure interpreter: `EffectiveTactics` is a
  derived interpretation, never a second board. `DefaultSlotAssignment` is the
  Runtime slot binding, with observation-order fallback only for synthetic inputs.
- Team FormationPresets are reusable data, never the current executed plan.
  src/runtime/formation_preset.cpp validates templates and converts candidates:
  TeamRelative x/y follow TacticalAnchor u/v ([0,1], [-1,1]); BallRelative
  offsets are metres (+x attack, +y team-left). One converter handles direction
  reversal and lateral mirroring without mutating templates or physical ball.
  Candidates are not legal placements; rules/planner legalization is deferred.
  TEAM-5/6 must wait for RFE Runtime RestartContext ownership. No Team orders
  or presets are currently read by Referee or positioning/Player AI execution.
- `tactical_examples.hpp` carries three pure-data example plans.

### Player AI (TACT-3, TACT-4, TACT-6b)

- `src/ai/eliza/eliza_intent.*` adapts a V2 plan to the existing Eliza
  heuristic through a read-only legacy-shaped view plus a bounded intent, so the
  legacy path stays identical.
- `src/ai/player/heuristic_policy.*` is `PlayerPolicyKind::Heuristic`: one joint
  per-team assignment (exclusive presser/cover, support, rest defence, marking)
  plus per-player on-ball/off-ball/keeper decisions. It is never the default.
- `IPlayerPolicyV2`/`PlayerAIContext` add read-only transient mental effects;
  `LegacyPlayerPolicyAdapter` keeps `IPlayerPolicy` implementations unchanged.
  Shouts never enter WorldState, the boards or a global.

### Coach tools and lifecycle (TACT-5, TACT-6a, TACT-7)

- `coach_command.hpp` is the typed command/envelope/result/log contract;
  `src/ai/coach/command_executor.*` commits atomically on a copy and bumps
  `TacticalBoard::revision`. `CoachRuntime` owns the queue, the log and the
  transient shouts, applies at the top of `AIEngine::Update`, and never owns a
  second board. `tool_registry.*` lists the nine v1 tools and own-side reads.
- `shout.hpp` + `coach/shout_resolver.*` define bounded mental effects with
  cooldown, a stack cap and fixed expiry.
- `coach_context.hpp` + `coach/context_collector.*` build a read-only,
  bounded-window context with deduplicated derived events; `coach/scheduler.*`
  owns periodic/event cadence, debounce and budget; `coach/heuristic_coach.*` is
  an opt-in NoOp/leading/trailing example. No provider, network or thread pool
  is introduced; `ICoachPolicy` is the future LLM seam.

### Online Coach Agent: protocol, tools, model and loop (AGENT-1..AGENT-3A)

The Coach Agent targets an OpenAI-compatible Chat Completions provider; local
models and training are out of scope. AGENT-1 lays the wire-independent,
network-free base:

- `football/model/model_protocol.hpp` is the provider-neutral conversation
  protocol (`ModelMessage`, `ModelToolCall`, `ModelToolResult`, `ModelResponse`),
  and `football/model/model_provider.hpp` is the seam (`IModelProvider`,
  `ModelRequest`, `ModelCompletion`, `ModelToolDefinition`). They carry no game
  type, so the loop, a mock and any future HTTP/local provider share one
  vocabulary. They live in the `football::model` leaf module, not in
  `football::foundation` and not in `football::ai`.
- `coach/coach_tool_names.*` is the single authority mapping every command
  enum to its lower_snake_case wire name, plus the per-key value kind.
- `coach/coach_tool_schema.*` builds the JSON Schema for the registry's nine
  tools in the OpenAI-compatible subset, so the schema and the registry cannot
  name different tools. `strict` only toggles the endpoint capability marker;
  validation is always local.
- `coach/coach_tool_decoder.*` validates one model tool call strictly and
  builds a typed `CoachToolRequest` (a read request or a `CoachCommand`).
  Argument JSON is untrusted: unknown fields, wrong JSON types, out-of-range
  values and invalid enums are rejected before any game data exists.

AGENT-2 and AGENT-3A add the loop and its transport seam, with no network:

- `model/mock/mock_model_provider.*` is `football::model_mock`: a scripted
  in-process provider that records every request, including its tools.
- `coach/coach_tool_json.*` serialises read payloads and the turn context with
  one vocabulary, so a read tool and the context agree.
- `coach/coach_tool_executor.*` turns a decoded request into a read payload
  or a *staged* command. A write result says `{"status":"staged"}` and
  carries the command out; it never touches a board.
- `coach/coach_session.*` owns one team's per-turn history, so a trim drops a
  complete exchange and never orphans a tool message.
- `coach/coach_agent_loop.*` runs model reply -> tool calls -> tool results ->
  continue, bounded by a model-turn and a tool-call budget. It builds a
  `ModelRequest` carrying the conversation and this team's nine tool
  definitions (`CoachModelTools`), and answers every call (so the transcript
  stays valid). On a budget stop or provider error it clears the partial
  proposal: an unfinished turn never stages anything. `CoachTurnResult.staged`
  is the complete, validated proposal for the turn.

The controlling `TeamSide` is never a model argument: the Agent Session
boundary and stays PRIVATE to `football_ai`; it never appears in a public
simcore header. `football::ai` depends on `football::model` PRIVATE, so an AI
module never links a transport it does not use. The HTTP provider (AGENT-3B),
the async host and the atomic batch commit are later stages (AGENT-4..AGENT-5).

### Acceptance

No full match is ever run. Validation is the bounded suites (`ai_unit`, the
runtime harness, the public-header smoke) plus a core-only build.
`unittests/ai` covers the schema, coordinates, interpreter, response, heuristic,
coach commands, shout lifecycle, context, scheduler and examples.

AGENT-1 adds the JSON Schema for the nine tools and the strict tool-call
decoder. AGENT-2 adds the session history, the staged tool executor and the
bounded agent loop. AGENT-3A extracts the provider-neutral `football::model`
protocol and seam plus the scripted provider, covered by the `model_unit`
suite; the HTTP provider is AGENT-3B.

## RFE: synchronous ruling authority and Runtime restart ownership

The phased migration from 819f087 is recorded in the RFE commits.
Product Referee stages now explicitly receive RulingSink: Submit only queues,
and Simulation::CommitPendingRulings validates the entire ordered logical batch
before dispatch through CommitRuling. Commit immediately after restart advance,
EvaluateOutOfPlay, CheckPendingFoul, accepted-touch consumption and the goal
checks (after restoring the original frame). Never clear uncommitted rulings at
Step entry or ResetSituation; Init/Stop are match lifecycle reset boundaries.

Stop, restart award, card, goal and ball-in-play have real referee producers.
Ordinary restart producers submit Stop then Award; Runtime owns goal
score/scorer/event commit and Player disciplinary writes. Card counts change
now, delayed send-off still executes at the original Player deadline; card
receipts prevent replay.

RFE-2 moved the only writable restart execution state into Runtime:
`src/runtime/restart_context.hpp` (`RestartContext`/`RestartState`/`RestartPlan`/
`RestartPhase`), `src/runtime/restart_controller.*` (scheduling, opening/
half-time ceremony, setup, timeout placement, readiness, release, grace) and
`src/runtime/restart_planner.*` / `restart_placement.*` (moved from referee).
The Referee owns only `Foul` and the offside reference; it submits
`AwardRestartRuling` (with the administration window) and `BallInPlayRuling` and
calls no `RuleCommandSink` lifecycle method. Runtime publishes rule-state side
effects back through `Referee::OnCeremonialSetup()`/`OnRestartTaken()`.
`PlayerTickContext`, `PlayerCommandInputs` and `WorldStateBuilder` borrow
`Simulation::restart_context()`; no `RefereeBuffer`/`GetBuffer`/
`RestartNeedsSimulation`/`GetRestartTarget` remains.

RFE-4 wires the static Team configuration into the Runtime planner:
`RestartPlanningTable` feeds borrowed `SetPieceOrders`/`FormationPresets`/
`ActiveLineupState` into `PlanRestart`. The taker comes from the declared
order via `football::team::SelectEligibleTaker` over nearest-first active
candidates (unavailable entries fall through, then candidate order = legacy
nearest; for FreeKick the direct list is tried before the indirect one, a
taker-selection compatibility fallback only, not a rule-category claim); the
awarded side uses an Attacking preset, its opponent a Defending preset, with
slots bound through `ActiveLineupState`, converted by `FormationAnchorToWorld`
and legalized by the existing rule pipeline. Presets are tactical preferences,
never referee-approved placements. Empty config and kinds without a preset keep
the legacy path; the opening/half-time ceremony is unchanged. The heuristic
Player AI already consumes `restart_target`, so no AI code changed.

RFE-3 adds the derived `PlayState` (`src/runtime/play_state.hpp`) and names the
`StepImpl` early return as `Simulation::ShouldSkipPlayerSimulation()`: a stopped
instant skips actors only when no restart is being organized and it is outside
the opening/half-time placement window. Pending restarts keep simulating so the
heuristic AI moves the sides to Runtime targets. `BallInPlayRuling` is the only
path that starts the ball-in-play clock.

The repaired missing goal-kick branch is an intentional rule behavior change.
Bounded `[ruling]` tests in `unittests/runtime/referee_ruling_test.cpp` cover
front/touch/goal boundaries, awards, atomic validation, disciplinary timing,
Runtime restart ownership, Team set-piece orders/presets and the derived
play-state transitions.

RFE-5 made `MatchEvent` the unified confirmed-event source: it moved to
`include/football/event/match_event.hpp` with `MatchEventRecord`/
`MatchEventCursor`, publishes foul/offside/taken/period events, fixes
pass/shot terminal semantics, and exposes `GameEnv::ReadEvents`. RFE-6 wires
the autonomous coach loop behind `AIConfig.coach_enabled` (default off):
`AIEngine::Update` commits the previous turn, then runs
ContextCollector -> CoachScheduler -> HeuristicCoachPolicy -> CoachRuntime on
the V2 boards, with `observed_tick`/`expires_at_tick`/
`expected_board_revision` frozen per turn and same-turn commands treated as one
transaction. `GameEnv` owns the event cursor and forwards new events;
`CoachLog()`/`CoachCalls()` expose the applied commands and bounded turns.

RFE-7 closed the Referee boundary: the rule interface no longer takes a
`RuleCommandSink` (`RulingSink` is the only output), foul and offside state are
value-typed (`PlayerId` plus frozen/read-only facts, no long-lived `Player*`),
and `RestartPlan` stores `PlayerId` with Runtime resolving actors through
`RestartRoster` at execution time. A Level 1 `referee_unit` exercises the
value-typed rule surface directly.

REF-8 makes the boundary a real module: `src/referee/` builds `football_referee`
(alias `football::referee`), a static library that consumes only value rule
facts (`rules::RefereeTickFacts`/`RefereeView`/`BallTouchFacts`/`RestartRuleFacts`
plus foul/offside value state) and emits `RefereeRuling`. Runtime samples every
fact -- including the `RestartReleaseFacts` used for the release verdict -- at
the exact call boundary and derives the Referee's value view from its own
`RuntimeTickFacts`; the Referee never includes a Ball, Player, TeamView,
RestartContext, Event or Simulation header. `referee_unit` links only
`football::referee` and foundation. Runtime-side `action_authorization` (which
needs Player/Event values) stays in the sim domain. `OnPeriodEnded` now takes
only the ended phase.

## PLAYER: independent Player module (feat/player)

The Player code was compiled into `football_sim_domain` through
`target_sources(PRIVATE ...)`. The feat/player work turns it into its own
archive so the dependency direction becomes
`foundation/geometry/pitch -> ball/animation -> player -> team -> runtime`.
Team owns and manages Player; Player never references Team.

### PLAYER-1: the Player archive exists

`src/player/CMakeLists.txt` now declares `football_player` (alias
`football::player`) with the 19 Player translation units and the public
`include/football/player/` value headers in its PUBLIC FILE_SET. The Runtime
contract target drops those headers and links `football::player`, and
`football_sim_domain` links it privately. The root adds the module before
Runtime (the contract target references it) and whole-archives
`football_player` alongside `football_sim_domain` so the existing libsimcore
exports, including unreferenced `GetRoleFromString`, are retained.

The archive is still transitional: it reads Runtime/Event/Observation/Referee
internal headers through the shared `src/` root, so its unresolved symbols
keep resolving from the domain. `src/runtime/libfootball_sim_domain.a` now
contains zero Player objects and `src/player/libfootball_player.a` contains
all 19. Debug and Release both stay 13/13 CTest and keep the A17 digest
`5bef607ddd567a09` (22 actors, 18 switches).

### Remaining stages (not yet landed)

- PLAYER-2/3: close the interface. The current Player -> up-layer edges are
  `observation::MentalImage`/`PlayerImage`/`mentalimage_sampling`,
  `event::TouchState`/`e_TouchType`/`touch_query`/`AcceptedTouch(Sink)`,
  `runtime::TeamMatchState`/`RestartContext`, and
  `rules::EvaluateActionAuthorization`/`IsGoalkeeperBackpass`. They must become
  Player-owned values/sinks or be supplied through the tick context before
  `football_player` can link without the domain; an interface link test
  (public headers + foundation/geometry/pitch/ball/animation only) is the gate.
- PLAYER-4: move `src/team/team_players.cpp` from the Runtime domain into
  `football_team` and link `football::player`, so `football::team` constructs and
  destroys the Player-owning Team itself. This needs the feat/team base (Team
  already owns Player there) and a dedicated integration window because it
  changes Team's CMake and the Runtime link edge.

### PLAYER-2A: shared value types move to their real owners

The game-mode enum moved to the neutral `include/football/match/game_mode.hpp`
and TeamSide to `include/football/match/team_side.hpp`, so Player and
observation no longer name a Referee/Runtime path (the old headers are
compatibility forwards). The pitch frame transform moved to
`include/football/pitch/pitch_frame.hpp` as inline geometry taking `home`
instead of a TeamSide. Touch classification and the actor-readable touch facts
became Player-owned values (`player_touch_type.hpp`, `player_touch_facts.hpp`);
`event::TouchState` borrows `PlayerTouchFacts` as a base subobject so a
same-Process accepted touch stays visible. The touch queries moved to
`player/player_touch_query.*`; the event path is an inline forward.

### PLAYER-2B/3: the interface is closed and Player links independently

- Observation: `player/player_observation.hpp` is a Player-owned read-only
  interface; `observation/player_observation_adapter.*` implements it over the
  captured history. Player no longer names `MentalImage`; sampling stays in the
  observation layer and the actor only asks for a sampled value.
- Team facts: `football/player/player_team_match_facts.hpp`;
  `runtime::TeamMatchState` borrows it, `PlayerTeamView` reads the base.
- Restart facts: `football/player/player_restart_facts.hpp`;
  `runtime::RestartContext` borrows it, Player reads `active`/`taker` live.
- Authorization: `player/player_authorization.hpp` defines the value and a port.
  `runtime/player_authorization_port.*` implements it, calling
  `EvaluateActionAuthorization`/`IsGoalkeeperBackpass` at the exact point the
  original checks ran. The rule logic stays in the rules domain; Player only
  consumes the returned value.
- Event output: `player/player_touch_event.hpp` owns `PlayerTouchEvent` and
  `PlayerTouchSink`; `event/accepted_touch*.hpp` alias them back for Event/Runtime.

Acceptance: `src/player` includes no `event/`, `observation/`, `runtime/`,
`referee/`, `ai/` or `team/` header (enforced at configure time by
`unittests/player_link/`). `football_player.a` has no undefined upper-layer
symbol, and the `football_player_link_smoke` executable whole-archives every
Player object while linking only foundation/geometry/pitch/ball/animation/
collision/BqLog plus the public `include/` tree. Debug and Release are 14/14
CTest and keep the A17 digest `5bef607ddd567a09`.

### PLAYER-4: Team owns Player in its own archive

The feat/team commits were merged into feat/player (one conflict in
`world_state_builder.cpp`: TeamView naming plus the PLAYER-2A frame
conversion). Then `src/team/team_players.cpp` moved out of
`football_sim_domain` into the `football_team` archive, which now links
`football::player`. Runtime composes the two Player-owning Teams and owns no
Player itself.

The final direction is Runtime -> Team -> Player -> Animation/Ball/Pitch,
with Runtime coordinating Referee and Event. `football_player` links no Team;
`src/player` includes no Team header. Debug and Release are 14/14 CTest and
keep the A17 digest `5bef607ddd567a09` (85 test cases).
