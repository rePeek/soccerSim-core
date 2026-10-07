# Gameplay Football

A heavily modified headless fork of
https://github.com/BazkieBumpercar/GameplayFootball.

## Autonomous match runner

Declare both teams, pitch, match rules and startup AI configuration explicitly:

```cpp
#include "app/fixtures/default_teams.hpp" // executable-only sample inputs
#include "env.hpp"

GameEnv game{football::app::fixtures::MakeDefaultHomeTeam(),
             football::app::fixtures::MakeDefaultAwayTeam(),
             football::model::MakeLegacyPitch(),
             MatchOptions{}, football::ai::AIConfig{}};
game.Start();
while (!game.Finished()) game.Step();
const MatchResult result = game.Result();
// Serialize/write result in the application, not GameEnv.
```

`GameEnv` owns one Simulation and one opaque policy. Its running step is only:

```text
Simulation::Observe → AI::Update → PlayerControlSet → Simulation::Step
```

It does not implement football rules, tactical reasoning, coach scheduling,
configuration parsing, output serialization or rendering. `DefaultAI` currently
serves as the total match-policy entry point; no CoachAI/GNN/LLM scheduler is
introduced in this stage. A future MatchAI may orchestrate those privately.

The public API is `Start`, `Step`, `Finished`, `Result`, `Observe`, `Stop`.
There are no live `controls()`, `tactics()` or `request_*()` channels, and no
reset/legacy lowercase API. One Step makes one simulation call (10 ms nominal);
no batching or implicit observation history is retained. After full time Step
is a no-op, including AI; a stopped Step/Observe throws `std::logic_error`.

Start requires a stopped runner; duplicate Start throws. Startup validates and
initializes local owners before publishing them. Stop is idempotent and releases
both owners; Stop followed by Start recreates the original declarations/config.
No runtime or concrete-policy accessor is exposed, and runners are non-copyable
and non-movable. Caller changes to constructor inputs cannot mutate a match.

### Phases, clocks and final results

Referee/sim owns `MatchPhase::{PreMatch, FirstHalf, SecondHalf, Finished}` and
completion. SecondHalf includes its kickoff ceremony. Match owns three clocks:
- `WorldState::tick`: monotonic simulation timeline, one tick = 10 ms.
- `regulation_time` (`TickSpan`): runs after the half's actual kickoff, including
  ordinary dead balls; stops at half time and full time.
- `ball_in_play_time` (`TickSpan`): accumulates only actual live-ball ticks,
  excluding all dead balls and ceremonies.

`half_underway` and `ball_in_play` project these rule facts. `in_play` is execution
authorization: a Ready taker can act before actual contact makes the ball live.
Snapshots are observations, never authoritative storage.

`MatchOptions::half_duration` is `TickSpan`, defaulting to `Minutes(45)`: two halves,
no duration scale, added time, extra time or penalty shootout in this stage. CLI
milliseconds are converted exactly on the 10 ms grid; positive off-grid inputs
are rejected. Sim validates native durations/full-match capacity before RNG draws.
Half time changes ends once at the next canonical between-tick frame. Each
half requires its own real kickoff. The referee whistles at the period boundary
even with a pending restart, before further contacts; terminal state is frozen.

`MatchResult` belongs to sim and contains `home_score`, `away_score`,
`MatchOutcome::{HomeWin, AwayWin, Draw}` and `duration_ticks`. Outcome is derived
from authoritative stable home/away scores; env forwards it without recomputing.
Result is available only after full time. Before then, before Start or after Stop,
Result throws `std::logic_error`: stopping is not a completed football match.
Copy the owning result before stopping. Retained results/snapshots survive teardown.

`duration_ticks` counts executed simulation calls, including the terminal referee
transition, not subsequent no-op calls. The terminal whistle does not advance
the timeline. Ordinary dead-ball positioning executes every tick without skips;
neither throughput nor wall-clock pacing changes any football clock.

`Observe()` is secondary replay/trace/debug telemetry. It returns an owning,
unscaled home-frame WorldState with ball motion, team scores/directions, player
kinematics, play/restart/retention and reset sequence.
There is no authoritative-state replacement or hidden per-tick snapshot history.

### AI intent and simulation replay

AI owns persistent desired/planned TacticalBoards, without transient input requests,
never actual simulation state. `AIConfig::initial_tactics` optionally supplies
startup boards by side; otherwise DefaultAI bootstraps them from static models.
These values are copied, not exposed as a live env mutation channel. DefaultAI
currently reads its base boards without rewriting them during Update.

The app has no external action protocol, player selection or human input path.
GRF actions/sticky state and their adapter are removed. Diagnostics/tests replay
explicit PlayerControlSet tapes directly through Simulation: equal initial
declarations and controls produce identical trajectories and RNG states, without AI.
DefaultAI's unused Request* channel, associated timers and sole Match lifetime marker
were removed after auditing real callers. No compatibility or future Coach mechanism
replaces them. Retained worlds/policies and Stop/Init replay remain value-only.
The baked animation path is sim-private, not a public build requirement.

## Static declarations and simulation boundaries

Model owns explicit team rosters, pitch, formation, abilities/appearance and
PlayerIds. IDs must be valid and unique across both rosters; empty rosters or
formations without profiles fail before RNG draws. Legacy database IDs are only
import provenance, never identity. Sample factories supply IDs 0–10 and 11–21
and live only in app fixtures, never core. Supported pitch geometry remains the
legacy 110 × 72 metres.

Simulation consumes explicit PlayerControlSet values, with idle fallback for
missing controls and execution-side legality. It has no policy fallback or input
ownership. Referee is the rules engine, not a moving actor.
Animation mechanics use the baked asset, never runtime XML/legacy importers.
There is no active-environment global, ScenarioConfig, GRF environment adapter,
controller hierarchy or byte-blob checkpoint API. Durable save/load would require
an explicit value state contract, not per-class memory hooks.

### Simulation domains

`src/sim/` keeps only `simulation.hpp` / `simulation.cpp` at the top level
(besides CMake). Concepts live with their football domain, irrespective of
whether they are exported:

```text
sim/
├── simulation.hpp / simulation.cpp
├── time/         Tick, TickSpan and boundary conversions (no actor timing policy)
├── player/       Player, PlayerControl/Set, execution commands and mechanics
├── ball/         Ball physics, touches and prediction timing
├── match/        Match, MatchClock, MatchOptions, MatchPhase and MatchResult
├── team/         runtime Team, formation adaptation and possession arbitration
├── observation/  WorldState, world_state_builder, pitch_frame and execution history
├── rules/        Referee, goal geometry, offside and restarts
├── animation/    baked clips, library and selection
├── query/        reachability and player queries
└── random/       simulation RNG authority (algorithm remains in foundation)
```

For example, AI includes `sim/observation/world_state.hpp` and
`sim/player/player_control_set.hpp`; apps use `sim/simulation.hpp` and
`sim/match/match_options.hpp`. WorldState remains an owning observation
projection, never mutable world authority. CMake exports define library/API
boundaries; there is no `contracts/` directory or legacy include-path shim.

Runtime authority migration is incremental. `Simulation::Step()` now dispatches
controls and invokes `ball/ball_player_contact.*`, applying its impulse, random
rotation and cooldown at the original point in the tick. The resolver receives
explicit domain inputs, not a Match/Simulation service locator.
`player/player_contact.*` owns ordered player-pair separation, movement sharing and
tackle/trip mechanics. Simulation passes players, Ball, possession
designation and Referee explicitly; notices remain synchronous after each trip.
No pair sorting, position snapshotting or deferred foul-event queue is introduced.
`rules/goal.*` provides a pure swept-segment predicate from Pitch and positions.
The caller keeps the legacy prediction gate and AdvanceTime → both goal checks →
score/scorer application order; geometry reads no Match, Ball, player or clock.

`Ball` owns only physics/predictions and a copied Pitch; netting receives the current
`BallEnvironment` rule fact on each call. Touch-dependent history/possession refresh
is composed externally by a transitional `Match::TouchBall` bridge, preserving
refresh-before-rotation timing. Player/Team touch accounting still notifies legacy
rules synchronously. `Simulation::Step()` now composes all legacy phases explicitly;
`Match::StepRemainingTick` is removed. `team/possession.*` evaluates best-team and
designated-player selection without publishing state. Simulation applies the selection
after roster refreshes; the separate physical `ballRetainer` fact remains Match-owned.
Match is not yet a state-only container; touch/reset/history bridges remain.
`Simulation::match()` remains a temporary test/diagnostic escape hatch.

Simulation directly owns the three-slot MentalImage history and capture cadence.
Observation sampling/newest-ball refresh take explicit spans; Match borrows this same
history only for transitional actor/touch/reset/mirror composition, never via a
Simulation pointer. MentalImage has no Match pointer or implicit clock/Ball reads:
capture takes tick, ordered player span and Ball; sampling takes explicit now/Ball.
Legacy Player deviation clamps and signed horizon quantization remain unchanged.

`match/match_clock.*` defines MatchClock: timeline/regulation/effective clocks, run flags and executed
ticks. It takes phase explicitly, atomically clips advances to the current period,
and returns admitted ticks. Simulation explicitly advances Clock, updates the Match-owned
recent possession window, then evaluates goals. Match::AdvanceTime/SwitchEnds are removed;
Simulation::ApplyChangeOfEnds preserves roster → roster → Ball → history order at tick entry.
Simulation::AdvanceTime is diagnostic-only (no physics/rules/execution count). Referee keeps
period decisions; Simulation keeps tick counting/ordering. Match now has no tick entry
or collision/goal/selection algorithm, but actor ownership and touch/reset/mirror
bridges remain. It is not yet ready to be renamed `MatchState` or removed.

`rules/period.*` defines pure PeriodElapsed from explicit underway/phase/regulation/duration
facts. Simulation's pre-contact gate and Referee's whistle boundary use the same predicate;
the no-argument Referee query is removed. A standalone value-only test proves this boundary.
Other Referee → Match dependencies and period/restart consequences remain transitional.

## Build and tests

Each module CMakeLists owns explicit sources, public FILE_SET HEADERS,
dependencies and `football::` aliases. `game` directly compiles `src/env.cpp`;
`src/env.hpp` is the public match façade. Its implementation is the sole product
composition root for sim + AI:

```text
football_app → football::game → ai + sim → model/foundation
```

Root CMake exposes model, sim contracts and AI startup contracts as PUBLIC usage
requirements; concrete sim/AI implementations stay PRIVATE. The header-only
`football_sim_contracts` build target exports selected domain value headers, not
entire directories. sim/animation is an independent archive also consumed by the
offline baker. AI links only value contracts, never sim actors.

```sh
nix develop --command bash -c 'cmake --preset release && cmake --build --preset release -j 4 && ctest --preset release --output-on-failure'
build/release/football_app                         # one complete regulation match
build/release/football_app --half-duration-ms=1800 # complete short regulation match
```

`--steps` is retired; app always loops until sim completion and prints MatchResult,
not a partial observation. Parsing/writing belongs to app. CLI/tool executable
paths remain at the build root. CTest runs the real app for short and full-match
black-box coverage; a separate smoke executable is unnecessary.

BqLog 2.5.0 is fetched via CPM at a pinned commit in all build modes; Catch2 is
fetched only with BUILD_TESTING enabled. Both are cached in `.cache/CPM`.
`-DBUILD_TESTING=OFF -DFOOTBALL_BUILD_APP=OFF` builds core only (first BqLog fetch
needs network); the args/fixture archive remains EXCLUDE_FROM_ALL. BqLog is PRIVATE
to game/sim. GameEnv starts the single `football` logger and flushes after teardown;
warnings log, operational failures throw, and debug invariants use `<cassert>`.
There is no `src/support/`: app profiles are typed fixtures retaining six-decimal
quantization, and legacy XML/codecs/text helpers live only in `tools/animBaker/import/`.
File I/O uses the standard library; custom diagnostics/backtraces and Properties
are removed. Neither app nor Catch2/offline baker code is linked into core.
Tests cover rule/clock completion,
results/lifecycle/restart, independent owners and AI-only value paths,
control-tape/RNG replay and baselines. Test-only programs live under `test/`;
`tools/` contains read-only regression/restart diagnostics and the offline baker.
The baker shares callable bake/check/verify operations with Catch2 tests, which
compare independent bakes byte-for-byte and verify fields/selection against source.
Semantic-stage motion/action/RNG changes and prior hashes are archived under
`test/baselines/`; the three-clock/scale-removal evidence is in `pre_three_clocks.md`.
Restart CSV definitions, multi-seed results and rerun commands are documented in
[`tools/restart-metrics.md`](tools/restart-metrics.md). These measurements do not
change restart bounds or claim football realism.
