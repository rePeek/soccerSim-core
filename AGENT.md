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
├── sim/              domain-organized rules, physics, execution and observation
│   ├── simulation.*  owns Match, RNG, MentalImage history and baked library
│   ├── time/         Tick/TickSpan, 100 Hz quantum and exact boundary conversions
│   ├── match/        Match, MatchClock, options, phase, result, pitch aliases, touch sink
│   ├── team/         runtime Team, formation adaptation and possession arbitration
│   ├── ball/         standalone Ball physics/environment, touch kinds/prediction timing,
│   │                 write-only touch event/sink and ball_player_contact interaction
│   ├── observation/ owning WorldState, world_state_builder, pitch_frame adapters
│   │                 and MentalImage/player-image history + nearest-slot sampling
│   ├── random/       simulation-owned RNG authority alias; algorithm in foundation
│   ├── animation/    baked schema/library/selector; depends only on foundation
│   ├── query/        player queries, reachability and force-field representation
│   ├── rules/        Referee, goal geometry, period boundary, offside and restarts
│   └── player/       Player, controls, commands, locomotion and player_contact mechanics
│       └── humanoid/ Humanoid / HumanoidBase / utilities
├── ai/               value-only decisions; never links sim runtime
│   ├── ai_config.hpp startup values; no live channel
│   ├── tactical_board.* persistent desired/planned values + model-only bootstrap
│   └── default_ai.*  WorldState + persistent tactical boards → PlayerControlSet
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
├── football_restart_metrics.cpp read-only restart/clock composition diagnostic
├── restart-metrics.md       definitions, measurements and reproducible reruns
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
├── ball_player_contact_test.cpp standalone Ball, contact cooldown/order and explicit history inputs
├── player_contact_test.cpp   explicit contact inputs, in-place pair order and no RNG/ball mutation
├── goal_test.cpp             standalone goal geometry, segment bounds and legacy side-net veto
├── period_test.cpp           standalone explicit regulation boundary and ceremony/phase gates
├── referee_state_test.cpp    explicit period operation changes only referee-owned facts
├── possession_test.cpp       arrival ranking, ties, hysteresis and retainer override
├── mental_image_history_test.cpp sole history, newest refresh, mirrors/reset/lifetime isolation
├── actor_history_test.cpp    tick-local actor history inputs, no implicit query/fallback
├── tick_test.cpp             typed arithmetic, overflow and non-grid boundary rejection
├── player_observation_tick_test.cpp publication/reset stamps, re-entry ages and history sampling
├── reachability_tick_test.cpp grid candidate rollouts, split horizons and continuous precision
├── sim_control_boundary_test.cpp controls/frames/reset/replay
├── sim_match_lifecycle_test.cpp phases/clocks/end changes/result/freeze
├── pitch_frame_test.cpp     half/order geometry, nonzero ball/velocity and control round trips
├── match_tick_test.cpp      timeline authority, restart bounds, freeze and invalid boundaries
├── match_clock_test.cpp      standalone clock clipping, run gates, execution counts and atomicity
├── match_fatigue_test.cpp   real-distance fatigue, excluding ceremonies
├── restart_placement_test.cpp + restart_placement_fixture.hpp
├── restart_readiness_test.cpp geometry, actor waiting, authorization/contact and frame contracts
├── anim_baking_test.cpp      two independent bakes, byte equality, field/selection verification
├── legacy_animation_import_test.cpp parser semantics, codecs and catchable failures
└── baselines/               historical policy/input/phase schema goldens
```

## Build ownership and architecture

Each module explicitly declares its sources, public `FILE_SET HEADERS`, aliases
and link edges in its own `CMakeLists.txt`. No source globbing or `sources.cmake`.
Root CMake directly builds `game` from `src/env.cpp`, publishes `env.hpp`,
and provides `football::game`. There is no env directory, env target or intermediate
engine object target. Runtime whole-archive packaging retains unreferenced runtime
symbols (including the currently unused GetRoleFromString).

```text
app → game (env.cpp) → ai + sim
ai → ai_contracts + sim_contracts → model/foundation
sim → sim_contracts + animation → model/foundation (PRIVATE BqLog)
app_support (args/fixtures) → model/foundation
anim_baking → legacy_anim + animation/foundation
```

- `football_sim_contracts` is a header-only build/export target, not a directory
  or a contract domain. It explicitly exports PlayerControl/PlayerControlSet,
  WorldState, MatchOptions/MatchPhase/MatchResult and the two time headers.
  Public values live with their domain; directory placement never exports actors.
  The former gamedefines umbrella is split among player, ball, team, match,
  observation and query. No compatibility headers or common/types layer remains.
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
- `GFOOTBALL_BAKED_ANIM_PATH` belongs only to football_sim PRIVATE compile definitions.
  The shared target helper owns include roots/C++23/PIC/output, not resource lookup.
  No public contract, foundation/model, AI, app or baker inherits the runtime path.

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
- `football_period_test` builds rules/period.cpp with value contracts/Catch2 only,
  proving PeriodElapsed has no Referee/Match/runtime dependency. It checks inclusive
  period boundaries, cumulative second-half limits, ceremony gates and maximum spans.
- `football_goal_test` builds rules/goal.cpp with only model/foundation/Catch2, proving
  goal geometry has no Match/Ball/runtime dependency. Lifecycle tests retain score,
  half/order attribution and period-whistle coverage through real Simulation steps.
- `football_match_clock_test` builds match/match_clock.cpp with value contracts/Catch2
  only. It checks both-period clipping, ceremonies/dead balls, exact large tick spans,
  atomic overflow rejection, terminal freeze and separate executed-step accounting.
- `football_sim_control_boundary_test` includes lifecycle and restart tests.
  Direct PlayerControlSet tapes cover every WorldState payload field and RNG state
  at all 400 frames, both processing orders, independent replay and Stop/Init replay.
  No policy or external action protocol is involved in this replay test.
  Restart placement covers 72 original-source cases: 6 restart types × 2 processing
  orders × 2 taker sides × 3 roster shapes (11/11, 3/2, 1/1).
  Pitch-frame regressions cover both halves × both processing orders, repeated
  physical end changes, exact player/ball distance preservation, input immutability
  and DefaultAI chasing the real ball instead of its ghost mirror.
  Readiness plans cover 48 mode/order/taker/end-change combinations, plus native
  ball-out classification/placement, minimum/timeout crossing, non-repeated reset/RNG,
  actual scheduled release and different actor-dependent waiting times.
  An additional 800-step value/RNG replay under both orders covers pending targets
  and replacement of a sent-off taker.
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
- `tools/football_restart_metrics.cpp` is a read-only native composition diagnostic:
  separate ordinary/ceremonial restart waits, period-censoring, timeout repair and
  regulation/effective ticks by half. It observes rule facts, never supplies timing
  policy or mutates actors/RNG. Short native runs in both processing orders check
  clock/call invariants; metric definitions/reruns live in tools/restart-metrics.md.

## Autonomous runner and runtime authority

`GameEnv(home, away, pitch, MatchOptions, ai::AIConfig)` copies declarations and
owns one Simulation and one opaque DefaultAI. No default constructor, copy/move,
reset, runtime accessor or live control/tactics/request API.

```text
app main → GameEnv::Start
         → until Finished: Observe → DefaultAI::Update → controls → Simulation::Step
         → Result (application serializes it)
Simulation::Step → explicit domain phases → Match-owned competition/actor state
```

- Simulation owns tick sequencing, pending end-change application, execution accounting
  and control dispatch. ball/ball_player_contact resolves passive body contacts from
  explicit players/teams, touch facts, history and Tick values; it does not accept
  Match/Simulation or draw RNG. Per-volume contacts publish touch facts through
  ball/ball_touch_event.hpp's write-only BallTouchSink and the MatchTouchSink runtime
  composition; Team::SetLastTouchPlayer is removed, so actors never reach Match for
  touch notification. Publication stays synchronous inside the tick. Never freeze touch
  biases before the sweep: later actors must see earlier touches in the same tick.
- Ball owns a copied model::Pitch, never Match/Simulation, Team, Player or RNG.
  Every prediction-mutating operation receives BallEnvironment (the current netting
  rule fact), not a stored duplicate of match goal state. Ball::Touch is physics only.
  ball/ball_touch_application.*::ApplyBallTouch composes Touch → newest history refresh →
  first/second processing-roster possession refresh from explicit inputs; callers then set
  rotation. Match::TouchBall is removed; Simulation::TouchBall is a diagnostic escape.
  Keep both original prediction recalculations and sample-zero publication timing unchanged.
- Match::Step/Process/StepRemainingTick are removed. Simulation::Step now spells out
  the legacy phase order and all frame/terminal/ceremonial boundaries directly.
  Match still holds competition state, actors and transitional touch/history sampling
  composition; this is not a completed Player/Team/Referee dependency migration.
  Simulation::match() remains a transitional test/diagnostic escape hatch.
- Contact extraction retains cooldown's strict >15-tick boundary and the original
  three RNG argument-expression draws after touch-dependent refresh, before cooldown
  publication. Tests cover standalone Ball/netting, contact feedback/controlled gates
  and explicit time/history sampling. Before/after regression --print-baseline and
  restart diagnostics for seeds 42/43/44 × both orders × default/symmetric fixtures
  (30000 ticks per half) are byte-identical. No golden, physics or policy change.
- player/player_contact owns the former humanoid-pair collision algorithm and private
  bounce accumulators. Inputs are Tick, ordered active-player span, const Ball and the
  designated possession player, never Match/Simulation. Simulation supplies first then
  second roster in the common contact frame after possession, before clock advancement.
  Pair offsets remain immediate; movement sharing is accumulated/applied afterward.
  Referee& is an explicit transitional dependency: TripMe → TripNotice executes inline
  before any later pair/offset. Do not buffer notices; rules inspect live positions.
  TripNotice is now a Match-independent foul-state operation with explicit now/Ball
  position, sampled after TripMe in the same physical frame. It still reads live actors,
  keeps foul facts in Referee and has no implicit-clock wrapper. Preserve strict >60
  touch grace, standing 2D/sliding 3D radii, double literals, severity arithmetic, immediate
  publication and duplicate-tackler gates. CheckFoul(now) also takes the evaluation instant
  explicitly and uses it for the strict recheck/expiry windows and card deadlines; its foul
  facts, penalty geometry and possession reads stay referee/actor-local. Restart scheduling
  and StopPlay consequences still read Match; this does not parameterize all of Referee or
  add a RulesContext.
  Player contact extraction preserves the same regression fingerprints and twelve
  seed/order/fixture diagnostic records; goldens and legacy arithmetic are unchanged.
- rules/goal owns pure CrossedGoalLine(Pitch, side, previous, current), preserving
  the original triangles, strict segment endpoints, bidirectional intersection and
  legacy side-net literals. Simulation retains the Ball prediction lookahead gate per
  side, live-ball gate and goal application: AdvanceTime → first/second goal checks
  → netting fact → frame restoration → score/scorer/own-goal facts. No score or
  player state is read or written by the geometric predicate; no semantics repair
  or new goal event pipeline accompanies this extraction.
  Goal extraction also keeps regression --print-baseline and all twelve diagnostics
  byte-identical; Release 32/32, Debug 31/31 (excluding full-match CLI) and core-only
  builds pass. No regression baselines or baked assets are changed.

- team/possession owns read-only best-team/designated-player arbitration over already
  refreshed teams in processing order. It returns pointers, preserving the signed team
  time comparison, tie fallback, unsigned +10 float ratio and strict <0.85 hysteresis.
  Simulation publishes bestPossessionTeam then designatedPossessionPlayer at the old
  phase boundary. Both selection fields and the distinct physical ballRetainer fact
  remain Match-owned; Team/Player possession refresh algorithms are not moved.
- Simulation owns std::vector<MentalImage> and CaptureMentalImage scheduling. Capture
  remains after Ball processing, before players, empty-or-ten-tick, newest-first with
  three slots. H4-A1/A2: StepPlayers passes a tick-local span through Team → Player
  → Humanoid, including SelectAnim/NeedTouch/GetBestCheatableAnimID/MovementSmuggle.
  No actor stores the span or samples through Match. Preserve the initial previous-delay
  sample before updating mentalImageTime, live deviation clamps, signed millisecond
  sampling, staggered cadence and all conditional sample points. H4-B moved touch composition
  out of Match. H4-A3 deleted Match::GetMentalImage, its borrowed vector member and the
  constructor history parameter; Match (and match.hpp) no longer knows MentalImage at all.
  The history is Simulation-only, reached by actors through the explicit tick-local span.
  Match::Mirror and ResetSituation are removed: Simulation mirrors teams → Ball → images
  and resets competition facts → clears history → resets Ball → both processing rosters
  at the old mutation points. Referee::Process receives a narrow synchronous reset action,
  forwards it to ordinary/ceremonial preparation, and never stores it. There is no
  delayed reset, replacement context or Simulation pointer in Referee. Sampling/refresh functions
  live in observation/mentalimage_sampling and take explicit spans/Ball. Simulation diagnostic
  sampling is the only remaining history query and sees the same objects actors sample.
  at the old teardown point and its storage outlives Match. MentalImage has no Match
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
  Match-owned recent possession window with unchanged float arithmetic, before goals.
  Match::AdvanceTime and SwitchEnds are removed. Simulation::ApplyChangeOfEnds keeps
  first roster SwitchEnds → second roster SwitchEnds → Ball Mirror → image mirrors,
  at entry before counting/controls, without changing transient mirror flags.
  Simulation::AdvanceTime is a transitional diagnostic clock-only entry (no rules,
  actors or executed-step count); product execution remains Step-only. Clock ownership
  stays in Match for now; neither that nor ballRetainer is derived selection state.
  Each of possession/history/clock extraction preserves regression --print-baseline
  and all twelve seed/order/fixture diagnostic records byte-for-byte. Final Release
  33/33, Debug 32/32 (excluding full-match CLI), standalone Clock and core-only builds
  pass; no golden, policy, physics or asset change accompanies these extractions.

- rules/period owns pure PeriodElapsed(half_underway, phase, regulation, duration).
  Simulation evaluates it before passive contacts and at the original whistle boundary.
  Referee's no-argument PeriodElapsed is deleted, not retained as a wrapper. Duration
  validity remains initialization/Clock authority; only SecondHalf doubles the threshold.
  Simulation::EndPeriod preserves EndHalf → Referee::OnPeriodEnded → SetMatchPhase
  → pending end change. OnPeriodEnded takes phase/tick/kickoff spot/team explicitly,
  mutates only referee facts and never reads Match. Final time retains the legacy foul
  facts; half time clears the pending foul and schedules kickoff at +10/+30 ticks.
  Physical sides/history still change at the next Step entry, not phase publication.
  Referee temporarily reads Match in ordinary Process, foul/offside/out-of-play/restart
  composition; opening phase publication is still transitional. No rules context.
- rules/offside owns pure GetOffsideLine(mentalImage, now, ball, defending_team_id,
  defending_side, futureSim_ms). Defending id/side, sampling instant and Ball are explicit;
  it no longer reads Match/Team. Preserve the second-deepest defender scan, the same
  in-place copy mutation, ball-ahead override, halfway zeroing and pitch clamp. Referee::
  BallTouched consumes those facts and still owns the offside-player decision list.
- rules/ball_touch_facts.hpp defines BallTouchFacts: now, touch player/team id, touch/
  defending Team pointers, in_play/in_set_piece/offsides flags, const Ball and the ordered
  all-active span. Match::SetLastTouchTeamID assembles them from competition facts and calls
  Referee::BallTouched(facts) synchronously; Referee reads no Match state there and derives
  defending active count/candidates from the span. Restart release still calls the
  consequence API (StartBallInPlay) and offside still calls StopPlay/ScheduleRestart,
  both of which remain Match-reading consequence composition for later cuts.
  MentalImage decoupling, clock/end-change orchestration and period extraction each
  preserve regression fingerprints and all twelve diagnostic records byte-for-byte.
  Latest Release 34/34, Debug 33/33 (excluding full-match CLI), standalone period
  and core-only builds pass; no golden, physics, policy or baked asset changed.

After these three cuts, Match has no tick entry or collision/goal/selection algorithm.
It still combines competition facts/clock, actor/referee ownership and transitional
touch/history-sampling bridges plus the possession window. Do not rename it to
MatchState yet: actor/rule upward dependencies and runtime composition must be removed
before deciding which remaining state is worth retaining separately.

Current phase order (composed directly by Simulation):
```text
ApplyControls → ResolveBallPlayerContacts → ProcessReferee → StepBall
→ CaptureHistory → StepPlayers → UpdatePossession → ResolvePlayerContacts
→ AdvanceClock → UpdateRecentPossession → EvaluateGoal → ApplyGoalFacts
```
Terminal-referee and ceremony early returns keep their existing frame restoration
and clock behavior; this sequence is not permission to reorder legacy phases.

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
  `test/player_action_tick_test.cpp`; old diagnostic digests project milliseconds.
  Humanoid requeue/tactical phase cadences use the same tick-local policy values
  and overflow-safe reduced-remainder staggering, retaining both roster schedules.
  Player touch/card-effect timestamps are Tick; unused possession-duration storage
  is removed. Touch-decay reads optional Tick instants and bounds only relative ages.
  Ball prediction horizons/cache durations live in sim-private `ball/ball_timing.hpp`;
  prediction generation iterates TickSpan samples with seconds from the quantum.
  MentalImage stores a Tick capture instant and derives TickSpan age. Transitional
  calculation sampling adapters retain legacy horizon quantization; coverage is
  in `test/prediction_tick_test.cpp`. Unused Ball/Player mean-history APIs and
  their write-only history storage are deleted, not replaced.
  Clock scale removal is a separate S2 semantic stage, not a unit-only rename.
  See tools/time-model-migration.md before rounding continuous arrival estimates.
- Simulation owns period-end lifecycle; Match owns phase/score and a MatchClock containing
  executed-step count, Tick timeline, TickSpan regulation and TickSpan ball-in-play.
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
  `rules/restart_readiness.*` owns pure home-frame plans and geometric predicates.
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
  in tools/time-model-migration.md.
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
  Card administration adds 1000 ticks to ordinary restart minimum/maximum bounds;
  player card effect remains due 600 ticks after the foul. No skips implement either.
  Initial/half-time ceremonial kickoff deadlines remain unchanged. There is no
  animation/waiting-mode switch; baked animation resources remain mandatory for
  player motion/actions/contact. Historical pre-readiness policy fingerprints are
  archived in test/baselines/pre_condition_restarts.md.
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

### Player observation time after T4b

Player publication/reset provenance uses optional Tick (unset is not tick zero);
re-entry age sums/maxima use TickSpan. The unused decision-queue shadow, stale
millisecond accessors and write-only per-query trace fields are deleted; the sole
PlayerDecisionQueue and continuity/oracle contracts remain. Tactical image delay
is computed only at the sampling call, not cached as Player state. Simulation's native
history API and Match's borrowed actor bridge use TickSpan and nearest-capture half-up
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
candidate rollouts live in test/reachability_tick_test.cpp. T4c changes no policy.

### Native timeline callers after T4d

GetActualTime_ms, BumpActualTime_ms and GetLastTouchTime_ms are deleted, not kept as
compatibility APIs. Touch-decay readers accept an optional Tick evaluation instant;
absence means now and explicit Tick zero remains zero. The ability-dependent decay
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

### Restart diagnostic matrix

### Restart diagnostic matrix

football_restart_metrics accepts an optional symmetric-input flag after seed/half/order.
It copies complete home declarations to away with distinct IDs and equal difficulty
before Init; no runtime intervention is involved. Short CTests cover both orders.
It also prints one definition-precise `metrics` row per half: open-play running
metres per side and accepted kicked touches split into shot/pass contacts, plus
per-half goals. Pass contacts are not completed passes and shot contacts are not
official shots-on-target; no reception or keeper-utility check exists yet. Counts and
goal totals are validated against the final match facts before printing.
The pre-fix full matrix exposed reversed execution chasing a ghost ball despite correct
observation projection. See tools/restart-metrics.md; those reverse samples must not be
treated as calibration evidence. Frame correctness must precede behavior tuning.

### First-roster execution frame correction

Between ticks the ball shares the first processing roster's frame, including
reverse processing. Referee/collision/first actor scopes turn only the OTHER roster;
the second-actor all-actor turn is restored with an unconditional ball turn at exit.
Possession rollout uses the same geometry. Referee no longer owns a special ball
compensation adapter. See test/baselines/pre_first_roster_frame.md for intentional
reverse-row change and causal pre-fix failure. Native reachability covers nonzero
ball positions in both halves/orders; long two-half tests pin actual open-play
kicks and exact replay, not desired scores. Full matrix/replays and unchanged normal
records are in tools/restart-metrics.md. Realism/behavior calibration is not complete.
