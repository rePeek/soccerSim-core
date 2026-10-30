# AGENT.md

Guidance for AI agents working in this repository. This file is a development
specification, not a changelog: it describes the rules that are true *now*.
Individual migrations do not add their history here.

## Project and working rules

`soccerSim-core` is a deterministic, headless football simulation in C++23,
originally forked from GameplayFootball / Google Research Football / blunted2.
Remote: `git@github.com:rePeek/soccerSim-core.git`. License: Apache-2.0; the
blunted foundation boilerplate is public domain.

- `main` is the primary development branch. Treat the checked-out files and the
  history as the source of truth.
- Do not create `src/core/` or `src/legacy/`.
- Structural changes must update this file's layout and target/path references.
- Finish changes with a local git commit. Never push; the maintainer does that.
- Do not casually edit `data/` or the canonical animation GLBs/metadata under
  `assets/`: they are inputs to physics/animation acceptance tests. Runtime
  `.simanim` files are generated build caches, never checked-in authorities.
- Do not run full matches to validate a module change. Unit suites are the
  gate; a full match is not.

## Build and test

CMake >= 3.24, Ninja, C++23, Python >= 3.10; use the `flake.nix` dev shell.

```sh
# Debug build + unit/interface suites (the normal gate)
nix develop --command bash -c 'cmake --preset debug && cmake --build --preset debug -j $(nproc) && ctest --preset debug'
# Core-only build, no tests
nix develop --command bash -c 'cmake -S . -B build/core -DBUILD_TESTING=OFF && cmake --build build/core -j $(nproc)'
```

BqLog (pinned) and nlohmann/json v3.11.3 (pinned, the coach tool-call JSON
boundary) are fetched in every build mode through `cmake/CPM.cmake`; Catch2
v3.7.1 is fetched only with `BUILD_TESTING=ON`. All use `.cache/CPM`.

Animation assets are compiled by `cmake/animation_assets.cmake`:
`assets/animations/player_14/{manifest.json,*.glb}` plus
`assets/skeletons/player_14.json` are the only animation source authority.
`tools/playerAssets/animation_assets.py bake` creates
`<build-dir>/assets/runtime/animations.simanim`; Runtime and asset tests depend
on `football_animation_assets` and consume that generated cache. Normal builds
need neither Blender nor the legacy `.anim` importer. `tools/animBaker` is an
explicitly selected recovery/provenance tool, excluded from the default build.
GLBs use Y-up logical joint axes; physics/cache metadata stays Z-up. Do not
copy Blender display-bone preview exports over canonical sources. See
`assets/README.md` for the format contract and authoring/validation commands.

### Module testing policy

Tests are scoped to the module being developed. Each layer proves a different
thing, and its cost is paid only when relevant.

- **Level 1 — module unit tests** (`unittests/<module>/`, label `unit`). A
  module proves its own physics, geometry, rules, actions, data and ownership in
  isolation, without a running match. This is the routine gate: a module is
  complete when its own unit tests pass.
- **Level 2 — interface and bounded integration** (the Player link smokes and
  the Team composition suite; labels `interface`/`team`). Independent linking,
  object lifetime, data passing and ownership across a real boundary. Run when
  a public interface or cross-module boundary changes.

Current test scope is `ball`, `pitch`, `player`, `team` and `animation` only:
`animation`, `pitch`, `ball`, `team`, `team_link`, `player`, `player_link`,
`player_animation`, `locomotion`, `loco_animation`. The foundation, model, ai,
collision, contact, event, referee and public-header suites and the bounded
runtime harness were removed with their modules parked; restore them from
history when one of those modules is back under active work. Nothing in the
current scope constructs a full Simulation or plays a match.

Rules:

- A module change must not require a full-match simulation.
- Long-running tests must be explicitly selected and bounded by a CTest
  timeout; they are never part of routine validation.
- There is no golden/fingerprint tier and no per-tick numerical digest. Tests
  assert physical correctness, state invariants and module contracts, not
  equality with a previous build's trajectory.

## Coordinate frame policy

There is exactly one physical coordinate system, the **Stadium Frame**:

- x is pitch length; y is pitch width; z is up. The centre spot is the origin.
- Axes never mirror. Home's goal is at -X and Away's at +X in the first half;
  the half-time change of ends only flips which end each team defends.
- `PlayerKinematicState`, `BallState`, colliders, `MentalImage`, `Snapshot`,
  `WorldState` and `PlayerControl` all use this one frame.

Consequences and rules:

- A team's orientation is `TeamOrientation { defending_direction }`
  (`runtime/team_state.hpp`). The attacking direction is the negation of the
  defending direction; never store it separately.
- `Pitch` owns geometry and the static physics scene only. It does not know
  Home/Away, half, or any per-tick coordinate conversion.
- Runtime is the only owner of team orientation and of the tick schedule.
  `SwitchTeamEnds` is the only permanent change; it flips a direction, not
  stored positions.
- Observation, rules and AI all read and emit the Stadium Frame. Any
  team-relative interpretation is an explicit `attack_direction`/
  `defending_direction` value, never an implicit mirrored frame.

There is no per-tick execution frame and no coordinate transform: the
observation, controls and stored physics are all the Stadium Frame.

## Authority and invariants

- **Physics is deterministic and RNG-free.** There is no random number
  generator in the simulation core. Behaviour that would otherwise be random
  must be derived from explainable state (skill, contact geometry, body
  orientation, ball velocity, fatigue). Do not replace RNG with a
  `PlayerId + Tick` pseudo-random sequence; that is the same thing renamed.
- **One authority per fact.** Ball state, player state, match state and team
  state each have exactly one owner (Ball, Player, Simulation,
  TeamOrientation/TeamMatchState). Derived projections (Snapshot, WorldState,
  MentalImage, TeamView, `PlayerTeamView`) are read-only and must be
  re-derivable from the authority, never a second source of truth.
  Team facts reach the Player through the per-tick `PlayerTeamView` borrow
  (direction, possession weights, designated player, goalkeeper). The Player
  caches none of them, so a same-tick Runtime write is visible to every later
  consumer without a publication step. `PlayerTeamView` is live Runtime fact;
  `PlayerObservation` is lagged perception - never merge the two.
- **Module dependency direction is one-way**: foundation/geometry/collision
  underpin Pitch, Ball and Animation; Contact depends on Player State, Ball
  and geometry, never Player Execution. Player Execution may use Contact;
  Team owns Players; Runtime composes all of them. AI, Model, Referee, Event
  and Observation consume value contracts; no lower module includes an upper one.
- `WorldState` contains no actor pointers, animation/command queues, mutable
  runtime or AI objects.
- `Team` owns its `Player` objects and no live match state; Runtime owns the
  match execution state around it.
- `Referee` consumes value facts and emits `RefereeRuling`; it does not write
  actors or runtime state directly. Runtime owns restart execution and event
  publication. Its rule state is value-only: an `OffsideSnapshot` captured at
  the last attacking touch, a `Foul` sanction, and an explicit advantage state.
  Run-time samples the physical evidence (`BallPlayKind`, the ball radius and
  previous centre, `denied_goal_opportunity`); the referee interprets it. The
  ruling path is atomic: Runtime validates the whole batch before any
  mutation, then commits each verdict exactly once.
- Offside follows Law 11: a defender's `DeliberatePlay` resets the offside
  position, a `Deflection` or `DeliberateSave` preserves it, and a goal kick,
  corner or throw-in cannot create an offence. Fouls are classified
  deterministically as careless/reckless/excessive (no probability, no RNG);
  DOGSO and the Law 12 penalty double-jeopardy are explicit rules.
- AI is a producer of `PlayerControl`; it owns no actor, match state or RNG.
- There is no player heuristic and no default player policy. Without an
  explicit intent (the Tactical Planner / Intent Runtime), AI emits no control
  and Player executes the neutral idle state. A failure never falls back to a
  renamed heuristic.
- Execution never selects a target. A pass requires an explicit
  `target_player` or `target_position` and is rejected otherwise; shot aim is
  the explicit direction/target, never an automatic goal-centre solve. Player
  also owns no periodic tactical-space rating: spatial queries belong to a
  separate SpaceAnalyzer, not a Player tick.
- `PlayerControl` is the one Command value. Continuous movement is
  `move_direction` + `desired_speed` (no Move/Accelerate/Decelerate action
  verb); discrete verbs are `ControlAction`. `Play()` accepts the same verbs, so
  the controller and one-shot request paths share one action enum and one
  command-build boundary.
- Command and animation are not one-to-one. `ThroughPass` resolves to a
  short/long pass from its target distance, `Beat` to the ball-control family,
  `Tackle` to `Interfere`, `SlideTackle` to `Sliding`; `Pull` is a defined but
  Unsupported Command. `Player::ActionFeedback()` reports the source Command,
  installed `e_FunctionType`, completion and the build status.
- A `Play` request is only executed when the command build returns
  `CommandBuildStatus::Accepted`. `Rejected`, `Unsupported` and
  `FallbackMovement` are reported in `ActionFeedback::command_status` and never
  counted as consumed action input, even though arbitration accepted the
  request.
- Touch feedback distinguishes `contact_due` (animation frame reached),
  `touch_prepared` (Player proposed a touch) and `touch_accepted` (Runtime
  committed it). Only the last confirms a real touch.
- PlayerExecution::RunActionTick is an orchestrator: its named phase methods
  preserve the original order. The committed `PlayerKinematicState` and the
  executor's `ActionState()` are the movement and action-clock authorities;
  `legacy_anim_state_` and `legacy_spatial_state_` are compatibility
  projections, never a second authority.

## Player and Contact libraries

`src/player/` owns two STATIC libraries (State and Execution), the header-only
Command INTERFACE target and the `football::player` INTERFACE aggregate.
`src/contact/` owns the independent `football_contact` STATIC library
(`football::contact`), its sources and public FILE_SET. Execution's declared
Contact dependency supplies the aggregate's link closure.
Player's model and Command targets are declared in `src/player/CMakeLists.txt`;
there are no state/command CMake subdirectories. Model/parser/private spatial
and observation files live beside player.hpp. Only execution has a subdirectory.

Player is the entity: identity, definition/body profile, capability, personal
match state (`PlayerMatchState`: touch record, fatigue, cards, desired
interception time), the authoritative `PlayerKinematicState`/
`PlayerGroundCollider` and the lifecycle. Everything
between an accepted Control/Play/React input and the drawn pose is the
execution process, owned by the internal `PlayerExecution` component
(`execution/player_execution.*`) that `Player` composes privately:

```text
Player
  ├── identity, definition, capability
  ├── PlayerMatchState        touch record, fatigue, cards, desired ETA
  ├── authoritative kinematics + ground collider
  ├── PlayerExecutionDiagnostics()   one snapshot, not per-counter API
  └── PlayerExecution
        ├── PlayerExecutionState  pending inputs, action timeline, playback,
        │                         scheduling/epochs, compat projection, audit
        ├── action tick phases    (PlayerExecution::RunActionTick)
        └── PlayerExecutionPort   the narrow per-call view of the Player

Runtime, beside the players (not inside them)
  ├── TeamOrientation       the only direction authority
  ├── TeamMatchState        the only team-state authority
  ├── PlayerBallTable       per-actor ETA / possession relation + staggering
  └── formations_           role / placement / lazy / controllable
```

Rules for that composition:

- `PlayerExecutionState` is the single copy of every execution field. There is
  never a second action timeline or motion state; `legacy_anim_state_` and
  `legacy_spatial_state_` are projections of the committed kinematics.
- Execution reads capability/match facts and submits authoritative changes
  through `PlayerExecutionPort`, bound for one call
  (`PlayerExecution::PortScope`) and released before returning. Execution
  never stores the port or a `Player*`, so it cannot reach back into the entity
  outside a call it was given. `PlayerExecutionPort::Actor()` exists only for
  the pointer-keyed pass solver and the Runtime rule port.
- `Player` remains the only writer of the kinematic state: execution computes
  the locomotion proposal and calls `CommitKinematics`, `CommitLocomotion`,
  `CommitLegacyActionMotion` or `ApplyPositionCorrection`.
- Team formation and team match facts are not Player state either: Runtime owns
  the formation table and `TeamMatchState`, and hands only the per-tick
  `PlayerTeamView` to execution and the `TeamView` to rules/observation.
- Runtime, Contact and Observation never see `PlayerExecution`: they drive
  `Player` (SetControl/Play/React/PrepareTick/CommitTick and the read-only
  observation accessors), which forwards. `PlayerExecution` stays inside the
  execution library.
- Execution diagnostics (locomotion re-entry/publication audits, query
  opportunity census) live in `execution/player_execution_diagnostics.*`, not
  in the Player header. Migration counters reach tests as one value,
  `Player::Diagnostics()`, never one accessor per counter.
- Reachability/possession is a relation, not Player state: Runtime owns the
  per-actor ETA and possession facts plus their 100 ms staggering phases in
  `PlayerBallTable`, and hands the actor its own `PlayerBallRelation` through
  the tick context. The Player stores no ETA, no possession flags and no
  schedule phase.
- `football::player_state`: definitions, kinematics, action state, body profile,
  command status. Header-only value contracts; no Command, Contact or
  Execution dependency.
- `football::player_command`: Control/ControlSet, active requests, passive
  reaction inputs and input-parameter validation. Header-only protocol over
  State, never Ball, Contact, Animation runtime or Execution. It does not
  dispatch actions; Execution consumes these values.
- `football::player_execution`: the concrete Player entity, its internal
  `PlayerExecution` component (control/request consumption, action tick phases,
  action timeline and playback, scheduling/epochs, compatibility projection) and
  command realization: action arbitration/mapping, cadence schedulers,
  continuous locomotion/body facing, reachability and pass solving, animation
  matching and touch preparation.
  May depend on Command, State, Contact, Ball and Animation. Pure motion and
  prediction APIs remain value-only and usable without a concrete Player.
- `football::contact`: value-only pair-contact and active/passive touch models,
  impulse arbitration, contact evidence and body/action geometry. May depend
  on Player State, Ball and foundation geometry/collision. Never depends on
  concrete Player, Player Command/Execution, Runtime or rule implementations.

Rules:

1. No circular static-library linking or linker-group workaround.
2. Cross-player adapters live in `src/runtime/simulation_contact.cpp` and
   `src/runtime/ball_player_contact.cpp`: read live Player inputs, call Contact,
   apply corrections/reactions and publish facts. Contact never writes actors.
3. No Player library may depend on Runtime, Referee, AI, Event, Observation or
   Team implementation headers.
4. Preserve the two contact chains: prepared action proposals are arbitrated
   before the sole Ball::Step and Player::CommitTick; player-pair contacts run
   afterwards, following possession refresh. Pairs read post-correction live
   states and submit reactions immediately; movement sharing follows the loop.
5. Contact evidence is not a foul verdict. Runtime freezes actor evidence and
   adds match context (including DOGSO); Referee adjudicates. A touch proposal
   is not acceptance: Player feedback confirms the committed fact later.
6. `unittests/player_link` whole-archives State/Execution and Contact, links the
   Command protocol alone, and tests Execution's public prediction API without
   actors, assets or Runtime. Protocol headers compile separately using only
   exported includes. Contact's independent link proof is not part of the
   current test scope (see the testing policy above).
7. Player model and input headers are flat in `include/football/player/`;
   execution contracts live in its execution/ subdirectory. Contact contracts
   live in `include/football/contact/`. Nested state/command/contact Player
   include paths are not supported. Flat motion/body/pass forwarding headers
   remain owned by Execution; their consumers link Execution, not Command.
   Model/Command/Contact boundary guards check exact FILE_SET ownership rather
   than inferring dependencies from directory names.
8. `src/` is never a PUBLIC include directory. Lower-library consumers compile
   from `include/` alone.
9. PlayerActionState is State. Input request/reaction values are Command; their
   arbitration is Execution. Continuous motion, capability profiles, prediction,
   pass solving, action scheduling, feedback, baked action motion, pose-to-body
   conversion and diagnostic shadows are Execution. Contact owns contact
   geometry and impulse models. Preserve algorithms and tick/publication timing.

## Physics and numerical conventions

- SI units: metres, seconds, kilograms, radians. The native tick is 100 Hz
  (10 ms); simulation never scales time.
- Ground physics keeps the empirical friction/deceleration values in `Pitch`,
  not standard Coulomb coefficients.
- Deterministic systems have no hidden mutable state, read no global/singleton
  and mutate only the state they are handed.
- Keep floating-point expressions explicit and ordered; do not reorder
  arithmetic in a way that changes results without an intentional, tested
  semantic change.
- Invalid inputs (non-finite geometry, non-positive dimensions/radii, bad
  intervals) are rejected at the owning module boundary (constructor or
  validator), not silently clamped downstream.
- Cross-module value contracts are plain values; no `Player*`, `Team*` or
  animation objects cross a public value boundary.

## Source layout

```text
include/football/    public headers, one directory per module
├── foundation/      math/ (Vector3, quaternion, scalar), time/ (Tick),
│                    geometry/ (shapes, collider), collision/ (sweep, hits)
├── animation/       baked schema/library/selector, playback, touch and pose
├── ball/            Ball, BallModel, BallState, contact/impulse integration
├── pitch/           Pitch description and the static ground/post/crossbar scene
├── contact/         value-only contact models, colliders, impulses and evidence
├── player/          flat model/input contracts + execution/ capability APIs
├── team/            TeamDefinition, formation templates, Player-owning Team
├── match/           neutral shared values (TeamSide, GameMode)
├── observation/     WorldState, Snapshot, MentalImage and their builders
├── event/           MatchEvent, EventLog, touch/rule-touch values
├── referee/         rule values, rulings and the referee
├── ai/              tactical board/plan/interpreter, coach commands and loop
├── model/           provider-neutral model protocol/seam (STL only)
└── env.hpp          public autonomous-match facade

src/                 implementations; public headers live in include/football/
├── foundation/      football::foundation, football::geometry, football::collision
├── animation/       football::animation_base (baker-shared) + football::animation
├── pitch/           football::pitch (pitch.cpp)
├── ball/            football::ball
├── contact/         football::contact (no concrete Player or Runtime)
├── player/          State/Execution static libs + Command protocol + aggregate
│                      flat model/parser + actor files; execution/ implementation
├── team/            football::team (validation, presets, team_players.cpp)
├── runtime/         football::sim_contracts + football_sim_domain:
│                      Simulation (clock/phase/score, ball, Teams, referee,
│                      touch state, history), TeamOrientation/TeamView/team_facts,
│                      possession, restart context/controller/planner/placement,
│                      contact adapters, WorldState/Snapshot builders, clock/result
├── observation/     mental image, snapshot history, temporal graph, slots
├── event/           event recognizer/log, touch state
├── referee/         football::referee (rules over value facts)
├── ai/              tactical_*, coach/
├── model/           football::model (header-only protocol/provider)
├── testing/         internal SimulationAccess (never product/actor-facing)
└── env.cpp          public facade implementation

assets/              canonical distributable assets (see assets/README.md)
├── animations/player_14/  100 Hz GLBs + football metadata manifest
├── characters/default/   static mesh/skin/material GLB variants + manifest
└── skeletons/            shared logical bone contracts
tools/               app (football_sim), animBaker (legacy recovery only),
                     playerAssets (stdlib asset compiler + Blender preview tools)
unittests/           one directory per active module (ball/pitch/player/team/
                     animation), including player_link and loco_animation
```

## Coding conventions

- Prefer explicit values and narrow interfaces over comments that restate code.
- A public header includes only what it uses; keep the module boundary clean.
- New cross-module data belongs in a value header under `include/football/`.
- Do not add `src/` expectations to a public header; `include/football/env.hpp`
  must compile with `-Iinclude` alone.
- Keep transitional adapters clearly named and remove them when the last caller
  is ported; do not leave dead compatibility state or write-only fields.
