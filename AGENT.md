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
- **NEVER simulate a full match.** Do not launch `football_sim`, drive
  `GameEnv`/`Simulation` end to end, or run any multi-tick/long match simulation
  as part of development or validation. A Debug full match does not finish and
  verifies nothing; it only burns time and hides regressions. See the explicit
  rule in "Build and test" below.

## Build and test

CMake >= 3.24, Ninja, C++23; use the `flake.nix` dev shell.
Development validation requires **Debug and core only**. Do not build Release
unless the maintainer explicitly requests it.

```sh
# Core build + the (currently empty) test scaffolding
nix develop --command bash -c 'cmake --preset debug && cmake --build --preset debug -j $(nproc) && ctest --preset debug'
# Core-only build
nix develop --command bash -c 'cmake -S . -B build/core -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Debug && cmake --build build/core -j $(nproc)'
```

BqLog 2.5.0 (pinned commit) is fetched by `cmake/CPM.cmake` in every build mode;
Catch2 v3.7.1 is fetched only with `BUILD_TESTING=ON`. Both use `.cache/CPM`.
`flake.nix` provides ccache and the root CMake sets `CMAKE_{C,CXX}_COMPILER_LAUNCHER`
to it whenever it is found, so local rebuilds are cached automatically; a missing
ccache is a silent no-op. `-j $(nproc)` uses all host cores.
**The test suite is small and unit-only.** `BUILD_TESTING=ON` fetches Catch2 and adds
`unittests/`; see the layout and the two targets below. The former golden/end-to-end
tier and `unittests/baselines/` are gone for good.

**Never run a full match.** The sanctioned validation is the bounded suites
(`ctest --preset debug`, or `-L unit` for the leaf modules and `-L runtime` for
the runtime harness). Do not run the `football_sim` app, `GameEnv`/`Simulation`
end to end, or any long/multi-tick match simulation to check a change: it is not
a test, it must not be used to "smoke test" a refactor, and it only burns time.
Measured: the app does not terminate within 60 s even with
`--half-duration-ms=100` in Release, because it loops `while (!Finished())` and
a tiny half duration does not bound how many ticks the match takes to reach full
time (a single Step is cheap -- see the harness baselines -- so this is not a
hang, just an unbounded loop). For runtime coverage use `unittests/runtime/`:
it builds a real Simulation, runs a handful of ticks and stops, under a
wall-clock CTest timeout, and never plays a match.
Core-only configuration needs network on the first dependency fetch:

```sh
cmake -S . -B build/core -DBUILD_TESTING=OFF
```

The app args/fixture archive remains an explicit `EXCLUDE_FROM_ALL` target even
when CLI/tests are disabled. It is never linked into the shared library.
Runtime reads the configured baked animation asset, never a data-dir env var.

### Pitch owns the static physics scene (header-only, no module)

`football.pitch` is a header-only INTERFACE target owning
`football::pitch::Pitch` (`include/football/pitch/pitch.hpp`). Pitch builds and
caches the whole static scene once: the ground plane, the four goal posts, the
two crossbars, the ground/goal parameters and the fixed environment (gravity,
wind). Goal-net damping parameters come from the same description. It depends
on foundation + geometry and never on ball/player.

Dependency direction: `football::pitch` -> `football::geometry` ->
`football::foundation`; `football::ball` and (later) `football::player` read
the immutable pitch scene. `football_pitch` must never link `football_ball`.
No C++20 module, no module toolchain, no extra CMake flags: any supported
compiler builds it.
### Build performance (diagnosed, not fully resolved)

`simulation.hpp` transitively includes `player_active_touch_shadow.hpp` (which
pulls `player_active_touch_model.hpp` and `football/ball/ball.hpp`) and
`player_baked_pose_sample.hpp` (which pulls the baked body-pose/contact-region
geometry). Touching any of those inline headers therefore recompiles ~40
translation units (including `src/env.cpp` and the 2360-line `humanoid.cpp`)
and relinks ~15 executables; a measured single-header change rebuilds 54 ninja
edges in ~30 s at `-j 4` (16 cores available). The deferred fix is to move
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
├── ai/              AIConfig, TacticalBoard and DefaultAI declarations
└── env.hpp          public autonomous match façade; values + opaque private owners

src/                 implementations; public headers live in include/football/
├── foundation/      CMakeLists.txt declares football::foundation (math/*.cpp)
│                    and football::geometry (header-only); collision/ has its
│                    own CMakeLists for football::collision
├── ball/            football::ball implementation: *.cpp + private headers
│                    (ball_netting.hpp; never exported)
├── ai/              default_ai.cpp, tactical_board.cpp
├── env.cpp          sole product composition implementation for AI + sim;
│                    compiled into libsimcore.so with everything else under src/
    ├── simulation.*  owns clock/phase/score, ball, rosters, referee, touch state, RNG and history
    ├── simulation_config.hpp match-level options value

    ├── event/        EventRecognizer/EventTransition, EventLog/MatchEvent, TouchState,
    │                 AcceptedTouch + AcceptedTouchSink and whole-step EventTrajectory
    ├── observation/ owning WorldState, world_state_builder, pitch_frame adapters
    │                 MentalImage/player-image history + nearest-slot sampling,
    │                 value-only Snapshot + preallocated SnapshotHistory + temporal_graph,
    │                 static PlayerSlotTable identity -> TeamSide mapping and a pure
    │                 shadow-only Snapshot touch inference
    ├── referee/      Referee state, facts/views/commands, goal, period, offside, ball-state
    │                 rules and restarts
    ├── runtime/      MatchClock/MatchPhase/MatchResult, Roster (owns the player
    │                 entities), TeamFrame/TeamMatchState (the only team
    │                 orientation and match state), TeamView/team_facts (borrowed
    │                 view + facts projection), possession (cross-team
    │                 arbitration) and roster_possession (per-roster aggregation),
    │                 match_config.hpp (TeamSetup + TeamSide), formation.hpp
    │                 (InitialFormation/TacticalFormation + BuildFormation).
    │                 There is no Team class and no src/sim/team/ directory.
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
        └── humanoid/ Humanoid / HumanoidBase / utilities. Since P2a Player computes
                      and commits locomotion (Player::CommitLocomotion via
                      PlayerMotion) and HumanoidBase only mirrors that committed
                      state into the legacy spatial state; Humanoid still owns
                      animation selection, sampling and the non-locomotion action
                      path. P2b enables the capability-driven acceleration/braking/
                      turn rate and removes the mirror; P3 shrinks Humanoid to
                      animation only.

cmake/
├── CPM.cmake
└── bqlog.cmake            private third-party diagnostics build

tools/
├── app/                    football_sim CLI: parse → GameEnv → complete match → Result
│   ├── args.* fixtures/     executable-side args and sample rosters/profiles
│   └── recording/           SnapshotArchive writer/reader (football_recording)
└── animBaker/              offline importers and callable bake/check/verify library + CLI
    └── import/legacy_*     offline-only XML, value codecs and required text helpers

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
ai → ai_contracts + sim_contracts → foundation/pitch/collision
sim → sim_contracts + animation + ball + pitch → foundation/geometry (PRIVATE BqLog)
app_support (args/fixtures) → sim_contracts/foundation
anim_baking → legacy_anim + animation/foundation
```

- `football_sim_contracts` is a header-only build/export target, not a directory
  or a contract domain. It links foundation/pitch and explicitly exports
  PlayerControl/PlayerControlSet, WorldState, MatchOptions/MatchPhase/MatchResult
  and the two time headers.
  Public values live with their domain; directory placement never exports actors.
  The former gamedefines umbrella is split among player, ball, team, match and
  observation. No compatibility headers or common/types layer remains.
- `football_ai_contracts` owns AIConfig and TacticalBoard declarations; it depends
  on foundation/pitch, not concrete policy. `football_ai` owns policy implementations.
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
- `football_animation` is a header-only INTERFACE target (format types,
  serialization, the library reader, and selection) depending only on
  foundation. The runtime and the offline baker share one definition, so the
  baker links no runtime archive.
- `football_legacy_anim` is an offline object target. `football_anim_baking` is a
  callable offline library; `football_anim_baker` is its CLI. The baker is
  self-contained: it links only its own offline objects and foundation, never
  sim/ball/pitch/simcore/animation archives.
- `tools/animBaker/` is the only developer tool tree (offline baker library + CLI);
  `unittests/CMakeLists.txt` is an empty placeholder awaiting the redesigned unit
  suite. The baker is built under BUILD_TESTING and never linked into core.
- Source ownership and dependency boundaries are maintained directly in each
  module's CMakeLists and reviewed alongside C++ includes. There is no centralized
  architecture validator or separate dependency allow-list.
- Public headers are consumed from the top-level `include/` root as
  `football/<module>/...`; `src` remains the include root for the not-yet-migrated
  `sim/` headers and for module-private implementation headers.
- Core must not gain graphics, Boost, app fixtures or offline animation imports.
  foundation must not depend on upper layers. sim must never depend on AI.
- sim ownership boundary: `Simulation` owns `std::array<Roster, 2> rosters_`
(the player entities), `std::array<TeamSetup, 2> team_setups_` (the
`PlayerDefinition` storage every `Player` references), and the only
`std::array<TeamFrame, 2> team_frames_` and `std::array<TeamMatchState, 2>`
`team_states_` (orientation and team match facts). `Player` owns only its own
identity, `FormationEntry`, kinematics and behaviour. `src/sim/player/` must
never reference `Team` or iterate the squad; team-level algorithms live in
`src/sim/runtime/` (possession, roster_possession, formation, team_facts).
Possession takes the roster plus the runtime-owned `TeamFrame`/`TeamMatchState`
directly and identifies a team by `TeamSide`, never by a `Team*`. A per-call
`PlayerTeamView` (borrowed roster slices + the live `TeamMatchState`) is how a
player sees its team. There is no runtime Team class: `Simulation` owns the
rosters, frames and states, `Simulation::TeamViewOf()` builds the borrowed view
on demand, and `Simulation::MirrorTeam/SwitchTeamEnds/ResetTeamSituation/
RelaxTeamFatigue` own the remaining team behaviour. Rules and Observation see
only `TeamView`.
- The team/ directory is gone. A team is now a relationship expressed by a
player's own `TeamSide`, the runtime's `TeamFrame`/`TeamMatchState` and the
`Roster` collection -- not an entity. Do not reintroduce a Team adapter: add
the behaviour to `Simulation` or to `src/sim/runtime/`.

### Motion state writers (P2 preflight audit)

Audited at 4f9dc4c. `PlayerKinematicState` has one *declared* authority but
several writers, and two of them write it **through** the Humanoid spatial
state. Nothing may be added here without stopping to review the authority rule,
and each row below must shrink as P2c/P2d proceed.

| Writer | Fields | Tick phase | Cross-state sync | RNG |
|---|---|---|---|---|
| `Player::CommitLocomotion` (P2a) | whole state: position, velocity, facing, bodyFacing, speed | mid-tick, inside `HumanoidBase::ProjectMovementState` | committed value is immediately re-derived from the Humanoid by `ApplySimulationMovementState` -> `SynchronizeKinematicState` | none |
| `Player::SynchronizeKinematicState` | position, velocity, facing, bodyFacing, speed + groundCollider | whenever the Humanoid spatial state changes | **reverse path** Player <- Humanoid; callers: `ApplySimulationMovementState`, `CalculateSpatialState`, `CalculateFactualSpatialState`, `HumanoidBase::OffsetPosition`, `Player::OffsetPosition`, `Player::ResetPosition`, `Player::PrepareTick`, `Player::ResetRuntimeState`, `Player::Activate` | none |
| `Player::OffsetPosition` | position (+ Humanoid: startPos, nextStartPos, positionOffsetMovement, decayingPositionOffset, currentAnim.positionOffset) | contact resolution, after movement | delegates to `Humanoid::OffsetPosition`, then syncs back | none |
| `Player::ResetPosition` | position, facing/angle (+ Humanoid start/next/previous) | restart placement, reset, activation | delegates to `HumanoidBase::ResetPosition`, syncs back, then `BeginSimulationAction()` | none |
| `Player::ResetRuntimeState` | runtime flags, then a mirror sync | situation reset | syncs back | none |
| `Player::Activate` | mirror sync after the Humanoid exists | roster activation | syncs back | none |
| `Player::Mirror` | `kinematicState.Mirror()` (position+velocity) and `humanoid->Mirror()` (start/next/spatialState) | `Simulation::Mirror` around actor processing | mirrors both copies separately; the kinematic oracle proves they agree | none |
| `HumanoidBase::CalculateSpatialState` | non-procedural path: `spatialState.position = startPos + currentAnim.positions[frame] + smuggle offsets`, plus movement/directionVec/angle/floatVelocity | `Humanoid::Process` before `ProjectMovementState` | syncs back | none |
| `HumanoidBase::ApplySimulationMovementState` | spatialState position/movement/directionVec/floatVelocity/angle, previousPosition2D, body direction | mid-tick | then syncs back | none |
| `HumanoidBase::CalculateFactualSpatialState` | zeroes velocity/movement on an outgoing special state | end of a legacy animation | syncs back | none |

Findings P2c/P2d must resolve, in this order:

1. `CommitLocomotion` is only *partly* authoritative: the value it commits is
   immediately re-derived from the Humanoid, so one fact still has two writers.
   They agree today only because the kinematic oracle enforces it.
2. `Player::OffsetPosition` and `Player::ResetPosition` write the position
   *through* the Humanoid and sync back, so collision and reset authority still
   lives in the Humanoid `startPos`/`spatialState`, not in the Player.
3. `previousPosition2D` is the next tick's motion origin; a contact correction
   only lands there via the sync-back order. P2c must make the corrected Player
   state that origin.
4. `Player::Mirror` mirrors two copies of the same fact, and
   `PlayerKinematicState::Mirror` deliberately mirrors position/velocity only.
   No stage may start rotating facing/bodyFacing here: that is a separate
   coordinate-system migration.
   the actions.

P2c-1 (done) unifies the *write point* without changing the producers or the
sync direction: `Player::CommitKinematicState(next)` is now the only function
that installs a `PlayerKinematicState` (it also keeps `groundCollider` in step),
and both `CommitLocomotion` and `SynchronizeKinematicState` go through it. It
`CheckSimulationKinematicOracle()`: right after a commit the Humanoid mirror is
still stale, so the oracle is asserted only where a sync completes
(`SynchronizeKinematicState`, `ApplySimulationMovementState`).
`ResetPosition`, `ResetRuntimeState` and `Activate` keep their legacy order, and
`Mirror()` keeps its atomic dual-state compatibility path until P2d.

P2c-2 (done) flips the *contact correction*: `Player::OffsetPosition` commits the
corrected position itself (only the position changes; velocity, facing,
bodyFacing and `speed` are left exactly as the previous commit installed them)
and then calls `HumanoidBase::ApplyPositionOffset(offset)`, which is the legacy
animation/motion bookkeeping (startPos, nextStartPos, previous position,
positionOffsetMovement, decayingPositionOffset, currentAnim.positionOffset)
*without* re-deriving the Player state. `HumanoidBase::OffsetPosition` remains
for corrections that originate inside the Humanoid (its own goalkeeper boundary
clamps) and still syncs back; those call sites are the remaining legacy path for
clamps) still call the legacy entry; those call sites remain for S0.2.

S0.1 (done) removes the *implicit* reverse sync for pure locomotion. The former
source-agnostic `Player::SynchronizeKinematicState()` is now
`Player::CommitLegacyActionMotion()`: an explicitly named entry that adopts the
motion the legacy Humanoid derived from animation root motion. It is still used
by `CalculateSpatialState`, `CalculateFactualSpatialState` and
`HumanoidBase::OffsetPosition` (legacy action paths) plus
`ResetPosition`/`ResetRuntimeState`/`Activate`/`PrepareTick`, which S0.2-S0.4
migrate. `HumanoidBase::ApplySimulationMovementState` is split into
`ProjectSimulationMovementState` (spatial-state projection only) and the legacy
`ApplySimulationMovementState` (projection + adoption). The pure-locomotion
branch of `ProjectMovementState` now only projects the state the Player already
committed and then asserts the mirror oracle, so the Humanoid can no longer
overwrite a locomotion tick. The roadmap is now S0 (authority), S1 (attributes
and motion), S2 (animation decoupling), S3 (collision and touch) and S4 (legacy
removal); old P2c-3/P2d map to S0.

S0.2 (done) moves the goalkeeper boundary corrections' *authority* to the Player
while keeping their evaluation where it is. `Player::OffsetPosition` is now
`ApplyPositionCorrection(offset)` + the action oracle, and
`ApplyPositionCorrection` is the shared commit point (corrected position first,
then the Humanoid bookkeeping) for corrections whose evaluation still lives in
the Humanoid's pass. The four goalkeeper clamps in `Humanoid::Process` call
`CastPlayer()->ApplyPositionCorrection(...)` with unchanged formulas, conditions,
order and timing; `Humanoid` is a (transitional) friend of `Player` for that.
`HumanoidBase::OffsetPosition` had no callers left and was deleted, so no
bidirectional position entry survives.

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
  `Simulation` to check composition and lifetime only -- two rosters initialise
  with live `PlayerDefinition` references, a few ticks advance the timeline,
  `Mirror` twice restores actors/sides/ball, `SwitchEnds` flips both directions
  without touching the between-tick frame, and `Stop` is clean and idempotent.
  It never plays a match and asserts no physics. Baselines (Debug): Init ~0.23 s,
  first Step ~0.77 s, later Steps ~0, Stop ~0.01 s. The CTest `TIMEOUT` is the
  real guard, because bounding the tick count cannot catch a call that never
  returns.
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
  Gravity, ground resistance, goal-net parameters and the static colliders all
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
  TeamMatchState, now and the physical retainer explicitly. runtime/roster_possession
  owns roster aggregation and the pre/post-player possession phases (moved out of
  player/); runtime/possession owns the cross-team arbitration. Simulation iterates
  each active roster in order; Team::Process/UpdatePossessionStats/
  UpdateDesignatedTeamPossessionPlayer are deleted. Team only publishes its
  resulting data (match_state_); no Team context/services are introduced and Player
  never receives a Team&.
  Unused Team::HasUniquePossession and restart query wrappers are deleted; restart
  facts are read from rules directly. Team has no Match pointer at all;
  until Player/Humanoid lifetime dependencies are removed. ApplyBallTouch supplies
  now/retainer to first then second possession refresh before spin/notification.
- Simulation owns std::vector<MentalImage> and CaptureMentalImage scheduling. Capture
  remains after Ball processing, before players, empty-or-ten-tick, newest-first with
  three slots. StepPlayers passes a tick-local span directly through Player → Humanoid,
  including SelectAnim/NeedTouch/GetBestCheatableAnimID/MovementSmuggle.
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
  `unittests/player_action_tick_test.cpp`; old diagnostic digests project milliseconds.
  Humanoid requeue/tactical phase cadences use the same tick-local policy values
  and overflow-safe reduced-remainder staggering, retaining both roster schedules.
  Player touch/card-effect timestamps are Tick; unused possession-duration storage
  is removed. Touch-decay requires an explicit Tick and bounds only relative ages.
  Player::Process consumes a stack-local PlayerTickContext (now, authorization,
  last-touch identity and a live half-underway gate). Simulation assembles it for
  EACH actor; no stored context/span/port. Fatigue reads the borrowed clock gate
  after Humanoid execution so accepted opening contact remains synchronous.
  Locomotion eligibility, scheduling and decision publications take explicit now/
  Ball/retaining-ball inputs. Player has no Match member/include/lookup:
  reset provenance consumes Tick, deactivation consumes Ball/Tick, and send-off
  consumes Ball/Tick/RNG. Team::Exit performs the former destructor reset before
  each deletion in the original roster order. Player constructors borrow Pitch,
  AnimationLibrary and the Simulation-owned live RNG explicitly; Humanoid/Base
  receive the same library and stream at construction. Resets and action draws
  never retrieve RNG through Match, and no stream is copied or reseeded here.
  Humanoid/Base Process, animation selection, touch prediction, movement-smuggle
  and physics-vector helpers require an explicit evaluation Tick and never read
  a clock through Match. Their remaining Match reads are physics/authorization.
  Ball prediction horizons/cache durations live in sim-private `ball/ball_timing.hpp`;
  prediction generation iterates TickSpan samples with seconds from the quantum.
  MentalImage stores a Tick capture instant and derives TickSpan age. Transitional
  calculation sampling adapters retain legacy horizon quantization; coverage is
  in `unittests/prediction_tick_test.cpp`. Unused Ball/Player mean-history APIs and
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
- `observation/pitch_frame.*` owns runtime/home-pitch conversion for observation and
  control execution. Team transforms derive from current dynamic side versus fixed
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
  entries with no Humanoid but keeps active/sent-off mirror behavior.
- Humanoid/HumanoidBase flattening is separate work. Animation cache, 10 ms step,
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
BakedAnimationSelector. Simulation loads/shares the library; Player, Humanoid and
HumanoidBase receive a const AnimationLibrary& at construction. Team::InitPlayers
forwards the library only during creation, without storing an animation service.
Actor clip lookup/selection/root positions and foot counterfactual diagnostics use
the injected library, not Match. Player retains it for activation/reactivation; its
lifetime remains Simulation-owned. Humanoid utilities consume explicit Ball, live
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

All existing scale call sites (the only places `height` becomes a pose/contact
scale today):

- `player_motion_constants.hpp:44` — `defaultPlayerHeight = 1.92f` (definition).
- `humanoid.cpp:181` — `height / defaultPlayerHeight` for the active-touch boot
  surface/contact-region strike point.
- `humanoid.cpp:2005` — `playerHeight / defaultPlayerHeight` for the touch
  ball-height projection.
- `simulation_baked_body.cpp:57` and `:80` — `GetBakedPoseSample(height /
  defaultPlayerHeight)` in `BeginBakedBodyTick` / `CompleteBakedBodyPreparation`.

`GetBakedPoseSample(scale)` stores the scale in `PlayerBakedPoseSample.scale`,
which flows into `BuildBodyCollider` and `BuildBakedContactRegions` and finally
`EvaluateBakedBodyPose`, which multiplies every bind offset by `scale` and
never re-applies height (root XY stays in `PlayerKinematicState`; only the
baked root Z is scaled). At this audit there was no weight field yet and no
other scale source; BODY-1.1–1.6 below replace this with one profile.

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
and `Humanoid::SelectAnim`'s Movement branch reorders the already-valid
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
- `player/animation_transition_state.hpp` (LOCO-2.6b): `AnimationTransitionState`
  advances one tick at a time; every transition starts from the previous
  tick's final blended pose, so a mid-blend interruption never snaps back to
  the original clip's first frame. `currentAnim.frameNum` stays int and is
  not deleted or retyped.

LOCO-2.6c wiring points (identified, not yet wired): a new clip is installed
in `humanoidbase.cpp:871` (idle init), `humanoidbase.cpp:1214`,
`humanoid.cpp:1135` (empty fallback) and `humanoid.cpp:1705` (SelectAnim
"make it so"). The transition must advance exactly once per tick, after
SelectAnim settles and before `SampleBakedPose` consumes the final pose.

### LOCO-2.6c/d wired pose blending

`HumanoidBase` now owns `AnimationTransitionState`, the last `final_blended_pose_
and a clip-change guard. `Humanoid::Process` calls `AdvanceMovementPoseBlend`
exactly once at the end: pure Movement blends across a clip change from the
previous final pose (mid-interruption safe), everything else copies the raw
pose and resets the transition. `SampleBakedPose` returns the final blended
pose, so the body/CCD tape derives from the same single pose. Integer
`currentAnim.frameNum` is untouched. `Simulation::EnableMovementPoseBlend`
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
`HumanoidBase::UsesProceduralLocomotion(ball_retainer)` (humanoidbase.cpp:1301)
gates both `CalculateSpatialState` (line 1311 early-returns; only `spatialState.foot`
is set) and `ProjectMovementState` (line 1505: pure branch only commits + projects;
non-procedural keeps root-motion adoption). So `currentAnim.positions`,
`movementSmuggle` and `actionSmuggle` are NOT read on the pure-locomotion
position path.

Classification:
- Category 2 (keep, non-Movement still needs): `CalculateSpatialState`
  non-procedural branch, `ProjectMovementState` non-procedural branch,
  `CalculatePhysicsVector` (touch/trap/ball-control prediction via
  GetBestCheatableAnimID + CalculatePredictedSituation), `currentAnim.positions`,
  `currentAnim.frameNum`, `movementSmuggle`/`movementSmuggleOffset`,
  `CalculateFactualSpatialState`, `CalculatePredictedSituation`.
- Category 3 (compatibility projection, no authority): the Movement-branch output
  of `CalculatePhysicsVector` (`positions_tmp` -> `currentAnim.positions`,
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
- P1 (fixed): `HumanoidBase::ResetPosition` now sets `bodyDirectionVec`/
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
6. `HumanoidBase::ProjectMovementState` pure branch reads
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

### ANIM-1.0 audit (read-only)

Which object owns the action clock: `Player::actionState.elapsed` (a
`PlayerActionState` stepped by `PlayerActionExecutor::Step` through
`Player::StepSimulationAction(TickSpan{1})`), called once per tick from
`Humanoid::Process` (humanoid.cpp:323). In parallel `HumanoidBase::currentAnim.
frameNum` is the legacy animation frame cursor, advanced at humanoid.cpp:322
and reset to 0 on a clip switch. `Player::CheckSimulationActionOracle` asserts
the two agree. `Player::Process` = `PrepareTick` + `CommitTick`; `PrepareTick`
runs the Humanoid pass (animation + action clock), `CommitTick` applies fatigue
and send-offs.

Which path owns the final Pose write: `HumanoidBase::SampleBakedPose` returns
the pose = `final_blended_pose_` (set once per tick by
`AdvanceMovementPoseBlend` at the end of `Humanoid::Process`) when valid, else
`clip.poses[frame]`. `Player::GetBakedPoseSample()` is the read-only facade;
`Simulation::BeginBakedBodyTick`/`CompleteBakedBodyPreparation` feed it through
`RealizeBakedPose` + `BuildBodyCollider`/`BuildBakedContactRegions`. The Humanoid
owns the final pose today.

Where active actions and Trip enter: active commands go
`PlayerControl -> BuildPlayerCommands -> PlayerCommandQueue -> decision clock ->
CommitLocomotion` (Movement) / `SelectAnim` (non-Movement clip choice). Trip is
passive: `player_contact.cpp` -> `Player::TripMe` -> `Humanoid::TripMe`, which
mutates the Humanoid interrupt state directly.

Humanoid dependency: animation selection, clip switch, frame clock, pose
sampling, non-Movement root motion, TripMe and the legacy spatial state all
live in the Humanoid; Player delegates heavily. Untouched modules: foundation,/
geometry, collision, animation (baked format/selector), ball, pitch, referee,
observation, ai. Covering tests: unittests/runtime (bounded), and the pure
unittests/player, unittests/locomotion, unittests/loco_animation suites.

Later ANIM-1 cuts (1.1 action lifecycle, 1.2 per-tick pose update, 1.3 active
transitions, 1.4 passive reactions, 1.5 scheduling) are recorded as they land.

### ANIM-1.1 action lifecycle interface

`player_action_request.hpp` defines `PlayerActionRequest` (kind + direction +
optional target/power -> `e_FunctionType`), `PlayerReaction` (Trip/Bump +
direction/severity/source) and `RequestOutcome` (Accepted/Deferred/Rejected).
Pure arbitration: a one-shot in flight rejects a repeat of the same action (so
repeated control input cannot restart it) and defers a different one until a
legal switch window; a physics reaction always preempts.

`Player::Play(request)` / `Player::React(reaction)` record the outcome and keep
the pending request on a per-player active/passive channel; `Player::Pose()`
returns the player's final `PoseFrame` (default when inactive). Execution is
deliberately unchanged: these are the stable request vocabulary the later
ANIM-1 cuts wire into the runtime, so the legacy PlayerControl/PlayerCommand
path stays authoritative for now. Every reset path (`ResetPosition`,
`ResetRuntimeState`) clears both channels, so no stale request survives a
reset/half-time/deactivation.

Tests: `unittests/player/player_action_request_test.cpp` (arbitration) and a
bounded runtime case asserting per-player independent Play/React channels, a
finite `Pose()`, and reset clearing.

### ANIM-1.2 per-tick pose update

Already unified by LOCO-2.6: `Humanoid::Process` calls `AdvanceMovementPoseBlend`
exactly once per tick for every action type (pure Movement blends; other
actions copy the raw baked pose), and `SampleBakedPose` returns that single
`final_blended_pose_` as the one `PlayerBakedPoseSample.pose` that physics and
snapshots share. Reading the pose advances nothing and consumes no RNG.
`Player::Pose()` is the read-only facade (default when inactive). A bounded
runtime test asserts the pose is idempotent within a tick and still advances
across ticks with no new control input.

Remaining ANIM-1 cuts (1.3 active Movement<->non-Movement transitions with
per-action durations and touch-frame integrity, 1.4 unified passive reactions
replacing direct `Humanoid::TripMe`, 1.5 scheduling consolidation) need the
Humanoid action-switch lifecycle and touch timing, and are tracked for later
focused work.

### ANIM-1.3/1.4/1.5

1.3 active transitions: `AdvanceMovementPoseBlend` now blends the pose across
ANY clip change, with `BlendTicksForSwitch(pure_movement)` = 8 ticks for pure
locomotion and 4 for a switch involving an action. The touch FRAME is still the
integer action cursor, so a blend cannot move the contact instant; the blended
pose is what the body contact geometry reads. Tests: the transition-state
per-type duration + a Movement->Shoot continuity case.

1.4 passive reactions: the contact trip sites in `player_contact.cpp` now also
submit a `PlayerReaction` through `Player::React`, so every real collision trip
is recorded on the unified passive channel (direction/severity/source/trip
type). `TripMe` still defers execution to the next Process, so the collision
and referee timing are unchanged; moving the interrupt state fully behind the
reaction entry is further ANIM-1.4 work.

1.5 scheduling: per-player runtime state (action clock, `final_blended_pose_`,
pending request/reaction) is already owned per Player and scheduled by stable
slot; a bounded runtime test asserts 4 players across both teams keep distinct
requests and that Stop/Init leaves no residue. Remaining: full Ground/StandUp
recovery, red-card-shrink and half-time frame checks are tracked for later.

### ANIM-1 P0 controlled-action harness

The bounded harness CAN execute real locomotion once the match leaves the
opening ceremony: with `PlayerControlSet` movement controls, `inplay` flips at
~tick 200 and the actor then moves and switches clips (a 240-tick run yields
`moved~6m`, `switches=2`). Earlier "no movement" observations were simply runs
shorter than the kickoff. A bounded runtime case now pins this so later ANIM-1
migrations have a real run to verify against (not only unit tests). Note the
cost: in-play ticks are much heavier than ceremonial ticks (~42 s for the
runtime suite at 240 ticks), so controlled scenarios stay short.

Still open under P0: Trap/Pass/Shot and Trip->Ground->StandUp need ball
possession and collision setups, and the React->Trip->Ground->StandUp execution
(P1) plus the Animation ownership migration (P2-P5) remain.

### ANIM-1 A1-A5 (Animation stateless; Player owns playback)

Revised scope: Player owns the action state, playback cursor, transition and
final pose; Animation is the shared stateless module (library + selector +
SamplePose + BlendPose). No second stateful AnimationRuntime.

- A1 `pose_sampling.hpp`: `SamplePose(clip, frame_time)` (integer exact,
  fractional SLERP/lerp, clamped, empty-safe). `unittests/animation/` now covers
  selection (`BakedAnimationSelector`), sampling and blending; the BlendPose
  test moved out of unittests/player into the animation module.
- A2 `loco_animation_transition_test`: Walk->Sprint->Shoot->Trip on the real
  baked library, no Simulation/Humanoid; switch jump 0.19 rad (smooth), touch
  frame fires exactly once.
- A3 `player_playback_state.hpp`: Player-owned `PlayerPlaybackState` (clip
  cursor + transition + final pose). A3.2 runs it in parallel with the Humanoid
  and a bounded oracle proved the pose bit-identical over 120 ticks.
- A5 the Player is now the pose author: `UpdatePlayback` feeds the selected
  clip/frame/base yaw, `GetBakedPoseSample()` returns the Player's pose and base
  yaw, and the Humanoid's `movement_transition_`/`final_blended_pose_`/
  `AdvanceMovementPoseBlend` plus its blend gate are deleted (the gate moved to
  the Player). `currentAnim.frameNum` is still the Humanoid clip selector's
  frame cursor feeding the playback; moving selection itself is later work.
- A4: 22 independent `PlayerPlaybackState` over one `AnimationLibrary` stay
  isolated, reset independently and replay deterministically.

Still open: clip SELECTION still lives in `Humanoid::SelectAnim` (the Player
consumes the selected clip); `React`->`Trip`->`Ground`->`StandUp` execution;
PlayerMotion owning non-Movement reference displacement.

### ANIM-1 A6.1 SelectAnim audit (read-only)

`Humanoid::SelectAnim` (humanoid.cpp:1158-1760) mixes four things:
1. Cheap culling for non-Movement (ball-distance early outs).
2. ReQueue gating: momentum comparison, requeue delays, same-type delays.
3. Candidate filtering + a stable sort chain: function type, incoming/outgoing
   velocity, side, incoming body/ball direction, properties, trip type, foot;
   then priority / idle / foot / body-direction / velocity / baseanim / catch
   sorts. This is `BakedAnimationSelector::CrudeSelection` + Player-state
   predicates.
4. Pick + install: Movement/Trip/Special take `*dataSet.begin()`; pass/shot/
   control/trap/interfere/deflect call `GetBestCheatableAnimID`, which builds
   touch vectors and is the only RNG-drawing part (via `rng_.Uniform` in the
   trap/control vector helpers). Installation mutates `currentAnim` and calls
   `BeginSimulationAction`.

So a naive `SelectClip` copy is wrong: the RNG-drawing touch scoring and the
action policy must stay on the Player side, and the request must carry any
randomness explicitly for reproducibility. `Player::Play` must be the decision
and playback owner; Animation supplies only stateless matching/sampling/
blending.

A6.2 delivers the pure Animation half: `select_clip.hpp`'s `SelectClip(clips,
request)` filters by action/velocity and returns the lowest-id match
deterministically (no RNG, no Player/Ball/Simulation). It is not yet wired into
`Humanoid::SelectAnim`; A6.3/A6.4 move selection onto `Player::Play` and remove
the Humanoid's choice, and A7/A8 (Trip chain, PlayerMotion displacement) remain.

### ANIM-1 A6.2 stateless SelectClip

`football/animation/select_clip.hpp` owns `ClipAction`, `SelectionRequest` and
`SelectClip`: `CrudeSelection` by function type (plus the incoming-velocity
bucket for Movement), then a deterministic lowest-id pick, returning an invalid
result on no candidates. Pure and independent of PlaybackState. Tested against
synthetic clips and the real baked library.

Remaining, tracked for the next focused work: A6.3 (`Player::Play` owns
selection + playback, with explicit RNG for touch scoring), A6.4 (remove the
Humanoid selection with an old/new oracle), A7 (React->Trip->Ground->StandUp)
and A8 (PlayerMotion non-Movement displacement).

### ANIM-1 A6.3a Movement shadow oracle (finding: hand-over blocked)

On each pure-Movement selection the motion-driven selector's choice is compared
with the legacy Movement candidate set (`LocomotionPlaybackAuditState()`'s
`shadow_checks`/`shadow_in_candidates`/`shadow_not_in_candidates`). Over the P0
tape (post-kickoff Movement) the result is `checks=35 in=34 not_in=1` and, on the
upgraded final-result comparison, `clip_match=34 clip_mismatch=1 fallback=1`: the new
selector picked a clip the legacy filter rejects on ~3% of Movement ticks.
Because the current reorder only wins when the choice is already a legal
candidate, A6.3b cannot blindly hand Movement selection to the Player without
changing the chosen clip on those ticks. Flipping safely requires moving the
legacy candidate filter + its fallback pick (still inside `SelectAnim`) too, or
an explicit decision to accept the change - not an oracle relaxation.

Remaining: A6.3b (Player owns Movement selection, must reproduce the legacy
candidate filter/fallback), A6.3c (AdvancePlayback timing), A6.4, A7, A8.

### ANIM-1 A6.3b.2 Movement refinement relocated (equivalence proven)

`include/football/animation/movement_selection.hpp` relocates the legacy
Movement candidate refinement - the config direction tables, the similarity
ratings, `_KeepBestDirectionAnims`/`_KeepBestBodyDirectionAnims` (strict
Movement form) and the Movement-relevant stable sorts - into a stateless
function `RefineMovementCandidates(library, crude, request)`. Step 1 landed the
module plus synthetic tests; step 2 added a per-tick shadow comparison in
`Humanoid::SelectAnim` against the legacy `dataSet` as an ORDERED sequence.
Over the P0 Movement tape: `refine checks=37 match=37 mismatch=0`, so the pure
function reproduces the legacy candidate list exactly (members and order).
`LocomotionPlaybackAudit` carries `refine_checks`/`refine_matches`/
`refine_mismatches`. The remaining flip (make the Movement branch call the pure
function and delete the duplicate filter/sort chain) must also rehome the
Movement-only `foot_counterfactual`/perturbation diagnostics that share the
sort chain.

### ANIM-1 A6.3b.2/b.3 Movement selection is Player-owned, legacy chain removed

The pure `RefineMovementCandidates` is now the authority for the Movement
candidate list. `Player::SelectMovement(library, crude, request)` owns the call
and retains both the ordered list and the foot-counterfactual list; the
Humanoid only supplies the spatial request (`pure_movement` branch) and reads
back `MovementCandidates()`/`MovementCandidatesWithoutFoot()`. The legacy
Movement strict filters and the Movement-relevant stable sorts no longer run, so
the Movement-only `foot_counterfactual`/perturbation diagnostics are fed from
the pure module's without-foot output instead. Equivalence was proven per tick
before deletion (37/37 ordered match), and the A6.3a final-clip oracle is
unchanged afterwards (`clip_match=34 clip_mismatch=1 fallback=1`), so the
hand-over is behaviour-preserving.

Remaining: A6.3b.3 install (moving the 25-field `currentAnim` write +
`BeginSimulationAction` from `Humanoid::SelectAnim` to Player), A6.3c (frame
advance ownership), A6.4, A7, A8.

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
Continuous reachability/reaction estimates and Humanoid SI/animation arithmetic
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
schedulers or clock accumulators. Humanoid's mentalImageTime is a typed reaction
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
not omitted bench profiles); inactive slots stay and do not dereference Humanoid.
`unittests/snapshot_capture_test.cpp` covers both halves/orders, ceremonies, terminal
duplicate ticks, sends-off, unequal rosters, jumps, resets and Stop/Init replay.
Release 36/36 and Debug 35/35 (excluding full-match CLI) pass unchanged goldens.
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
No extra Player/Humanoid execution, RNG, touch/rules writes or Golden refresh.

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
is a pure `BallImpulse{impulse, contact_point}` value. Humanoid observes the
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
`the removed design notes` records the exact `StepImpl`/`Humanoid::
Process` dependency table (which stages draw RNG, which advance animation,
where `ApplyBallTouch + SetRotation` take effect) and the plan: split
`Humanoid::Process` into one prepare pass (motion/animation frame/touch vector)
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
and send-offs after accepted touch consequences. Humanoid proposals go through
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
is captured during Humanoid's one real pass. `prepared_impulse` identifies the
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
boundary. Do not re-split Humanoid or redesign Ball's member API. Current roadmap
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
longer adopts the Humanoid state after `Humanoid::Process`: every path already
finished its own write inside that call (the pure-locomotion branch commits and
projects; each legacy root-motion branch adopts where it produced the position,
in `CalculateSpatialState` / `CalculateFactualSpatialState`). Re-adopting there
was identity at best and a hidden re-read at worst.

`HumanoidBase::previousPosition2D` is documented as a legacy animation cache, not a
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
`ResetPosition`, `ResetRuntimeState` and `Activate` keep adopting the Humanoid's
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

### ANIM-1 A6.3b.3 first cut: one install entry, Player-owned selection

`football::sim::SelectedAction` (clip id, function type, start frame, duration,
touch frame, touch position) is the prepared selection result. All three install
sites in the Humanoid now call `Player::InstallSelectedAction(selection)` instead
of `BeginSimulationAction()` directly, so the selector prepares the result, the
compat `currentAnim` is synced first, and the Player commits it through one
transaction. `Player` records the selection and cross-checks it against the
definition the executor committed (`InstallChecks`/`InstallMatches`/
`InstallMismatches`): clip/duration/start frame/touch frame/touch position. Over
the P0 tape `install checks=2 match=2 mismatch=0`, and the A6.3a final-clip
oracle is unchanged. The compat `currentAnim` storage and all its readers are
untouched, so this cut is behaviour-preserving; state ownership moves next.

### ANIM-1 A6.3b.3 second cut: the Player authors the action definition

`Player::InstallSelectedAction` no longer reads the definition back from the
Humanoid: it builds `PlayerActionDefinition` from the Player-owned
`SelectedAction` (type, duration, contact frame/position) and establishes the
start cursor from `selection.start_frame`. The compat Humanoid `currentAnim`
projection is still synced by the selector first and is now the thing the oracle
cross-checks (`InstallChecks`/`InstallMatches`/`InstallMismatches`), while
`CheckSimulationActionOracle()` still guards the schedule.
`install checks=2 match=2 mismatch=0` over the P0 tape and the A6.3a final-clip
oracle is unchanged, so the authority transfer is behaviour-preserving. The
`currentAnim` storage and its readers remain; retiring them is the next cut.

### ANIM-1 A6.3b.3 third cut: same-tick install consistency oracle

`Player::InstallSelectedAction` now also checks, immediately after committing,
that the action cursor, duration, scheduled-contact presence and contact frame
all match the selection (`InstallCursorChecks`/`Matches`/`Mismatches`). This is
the "install then sample in the same tick" guard: it catches a one-tick cursor or
contact offset between the Player cursor, the selection and the compat
projection. `cursor checks=2 match=2 mismatch=0` over the P0 tape.

### ANIM-1 A6.3b.3 fourth cut (playback cursor ownership): attempted, blocked

Making `Player` the authority for the playback cursor advance (replacing
`Humanoid::Process`'s `currentAnim.frameNum++` with
`currentAnim.frameNum = CastPlayer()->AdvancePlaybackCursor()`) trips
`Player::CheckSimulationActionOracle()`: `currentAnim.frameNum` is also written
directly outside `Humanoid::Process` (e.g. the `ResetPosition` path in
`humanoidbase.cpp`), so the projection desynchronises from the executor cursor.
Reverted. The cut needs every direct `currentAnim.frameNum` writer routed through
the Player cursor API first; that inventory is the prerequisite.

### ANIM-1 A6.3c.0/c.1: cursor audit + unified start-of-action install

A6.3c.0 audited every `currentAnim.frameNum`/`animationId` writer:
`humanoid.cpp` 326 (tick `++`), 1146-1147 and 1806-1808 (install `= 0`),
`humanoidbase.cpp` 628 (base tick `++`), 888-891 (ResetPosition idle + RNG start
frame), 1232-1234 (base install `= 0`); `previousAnim_frameNum` at
`humanoidbase.cpp` 631/909/1230 and `humanoid.cpp` 330/1803. `PlayerActionExecutor::Step`
saturates at `duration` (`elapsed += min(delta, duration - elapsed)`) while the
legacy `frameNum++` is unbounded; `unittests/player/player_action_cursor_test.cpp`
pins saturation, non-zero start frame, single contact fire and single completion.

A6.3c.1 routes every "start of action" path through the one install entry:
`ResetPosition`, `ResetSituation` (runtime reset) and `Activate` now call
`InstallSelectedAction(CastHumanoid()->CurrentSelectedAction())` instead of
`BeginSimulationAction()`. The Reset RNG draw is untouched - the random idle
start frame simply travels as `SelectedAction::start_frame`. `installed_action_`
is now valid after initialisation and every reset.
`install checks=11 match=11 mismatch=0`, `cursor checks=11 match=11 mismatch=0`
over the P0 tape; the A6.3a final-clip oracle is unchanged. Remaining for c.2:
flip the per-tick `frameNum` advance to the Player authority.

### ANIM-1 A6.3c.2: the Player owns the per-tick action clock

`Player::AdvanceActionTick()` is now the single real time advance: it steps
`PlayerActionState` (the authority), projects the cursor onto the compat Humanoid
`currentAnim.frameNum` via `HumanoidBase::SetCompatFrameNum`, and only then runs
`CheckSimulationActionOracle`. Both Process paths (`humanoid.cpp` and
`humanoidbase.cpp`) call it in place of their separate `currentAnim.frameNum++` +
`StepSimulationAction`. Ordering is therefore advance -> project -> check, which
is why the earlier projection-only attempt failed. The legacy `frameNum++` was
unbounded while the executor cursor saturates at `duration`; the projection now
makes the Humanoid frame follow the saturated authoritative value.
`previousAnim_frameNum` keeps its legacy semantics for now.
`install checks=11 match=11 mismatch=0`, `cursor checks=11 match=11 mismatch=0`,
and the A6.3a final-clip oracle is unchanged.

### ANIM-1 A6.3c.3: the Player samples from its own clock

`Player::UpdatePlaybackFromAuthority(base_angle, blend_ticks)` takes the clip id
from `InstalledAction().clip_id` and the frame from `actionState.Frame()`, so the
Humanoid no longer hands over clip/frame at all - it only supplies the
not-yet-migrated base yaw (`startAngle + rotationSmuggleOffset`) and the blend
length. `SamplePose`/`BlendPose` are untouched, and the same-tick install still
samples the new clip immediately (the A6.3b.3 cursor oracle covers the cursor;
the A6.3a final-clip oracle is unchanged).

### ANIM-1 A6.3d: authority audit - the compat layer no longer decides action time

Audited field ownership after A6.3c:

| currentAnim field | owner now |
| --- | --- |
| `animationId`, `frameNum` | Player `installed_action_` + `PlayerActionState.elapsed`; the Humanoid copy is a read-only projection (`SetCompatFrameNum`) |
| `functionType`, `touchFrame`, `touchPos` | Player `ActionState` / `SelectedAction` (mirrored for legacy readers) |
| `originatingCommand`, `originatingInterrupt` | still action-install context on the Humanoid (migrates with the non-Movement execution path) |
| `positions`, `incomingMovement`, `outgoingMovement` | still Humanoid (migrates to PlayerMotion in A8) |
| `movementSmuggle`, `actionSmuggle`, `rotationSmuggle` | still Humanoid; `rotationSmuggle` must be split by use (pose correction vs real body yaw) when it migrates |

`Player::UpdatePlaybackFromAuthority` now also verifies the invariant that the
compat projection cannot re-decide anything: the installed selection's clip id,
the action cursor and the clip/frame the pose was actually sampled from must
agree (`AuthorityChecks`/`Matches`/`Mismatches`). Over the P0 tape
`authority checks=42 match=42 mismatch=0`, alongside
`install checks=11 match=11 mismatch=0` and `cursor checks=11 match=11 mismatch=0`;
the A6.3a final-clip oracle is unchanged. No new action or time state was added.
