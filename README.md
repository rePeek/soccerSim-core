# Gameplay Football

A heavily modified headless fork of
https://github.com/BazkieBumpercar/GameplayFootball.

## Autonomous match runner

Declare both teams, pitch, match rules and startup AI configuration explicitly:

```cpp
#include "app/fixtures/default_teams.hpp" // executable-only sample inputs
#include "gameenv.hpp"

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
completion. SecondHalf includes its kickoff preparation; the football clock
pauses during stoppages. WorldState projects `phase` and `match_time_ms` for
telemetry and future coach decisions, but is never authoritative storage.

`MatchOptions::half_duration_ms` defaults to 45 minutes: two regulation halves,
no added time, extra time or penalty shootout in this stage. Half time changes ends:
the referee requests it, the simulation applies it once at the next canonical
between-tick frame, and each team then attacks the opposite goal. `match_duration`
retains the legacy compressed-clock scale (`factor = value * 0.2 + 0.05`), not
a tick budget. Sim rejects invalid/non-advancing scales and invalid/overflowing
period durations before RNG/profile draws. Clock increments are scaled/truncated
per step, accumulated as integers and clipped to period boundaries. The referee
whistles even if a restart is pending and before that boundary tick's ball contact;
the terminal match cannot advance clocks, players, ball state, actions, scores or RNG.

`MatchResult` belongs to sim and contains `home_score`, `away_score`,
`MatchOutcome::{HomeWin, AwayWin, Draw}` and `duration_ticks`. Outcome is derived
from authoritative stable home/away scores; env forwards it without recomputing.
Result is available only after full time. Before then, before Start or after Stop,
Result throws `std::logic_error`: stopping is not a completed football match.
Copy the owning result before stopping. Retained results/snapshots survive teardown.

`duration_ticks` counts actually executed simulation steps (including the terminal
referee transition), not subsequent no-op calls. Existing `WorldState::tick` is
compressed elapsed simulation time divided by 10 ms; restart fast-forwards can
make it differ from the executed-step count. `match_time_ms` is the separately
scaled football clock, not either of those elapsed/count values.

`Observe()` is secondary replay/trace/debug telemetry. It returns an owning,
unscaled home-frame WorldState with ball motion, team scores/directions, player
kinematics, play/restart/retention, reset sequence and an opaque Match epoch.
There is no authoritative-state replacement or hidden per-tick snapshot history.

### AI intent and simulation replay

AI owns persistent desired/planned TacticalBoards and short-lived requests,
never actual simulation state. `AIConfig::initial_tactics` optionally supplies
startup boards by side; otherwise DefaultAI bootstraps them from static models.
These values are copied, not exposed as a live env mutation channel. DefaultAI
currently reads its base boards without rewriting them during Update.

The app has no external action protocol, player selection or human input path.
GRF actions/sticky state and their adapter are removed. Diagnostics/tests replay
explicit PlayerControlSet tapes directly through Simulation: equal initial
declarations and controls produce identical trajectories and RNG states, without AI.
DefaultAI's transient Request* APIs and their lifecycle tests remain unchanged
pending a separate AI-ownership audit; neither app nor GameEnv exposes them.

Requests bind to owning, equality-only `WorldState::simulation_epoch`, tick and
reset sequence. New Matches cannot reactivate old requests. The epoch retains
no actors and uses no counter/RNG/clock; it is an in-process identity, not a
serialization ID. Synthetic logical matches must bind fresh epochs explicitly.
Physical regression hashes exclude epoch identity.

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

## Build and tests

Each module CMakeLists owns explicit sources, public FILE_SET HEADERS,
dependencies and `football::` aliases. `game` directly compiles `src/gameenv.cpp`;
`src/gameenv.hpp` is the public match façade. Its implementation is the sole product
composition root for sim + AI:

```text
football_app → football::game → ai + sim → model/foundation
```

Root CMake exposes model, sim contracts and AI startup contracts as PUBLIC usage
requirements; concrete sim/AI implementations stay PRIVATE. query/rules/player
stay sim internals; sim/animation is an independent archive also consumed by the
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
`tools/` contains the regression diagnostic and offline animation baker. The baker
shares callable bake/check/verify operations with Catch2 tests, which compare
two independent bakes byte-for-byte and verify fields/selection against source.
Motion/actions/RNG checkpoint goldens are unchanged; adding phase/time changes only World hash schema
at those checkpoints (see `test/baselines/pre_match_runner.md`). Historical strategy
and input schema hashes remain in the other files under `test/baselines/`.
