# Gameplay Football

A heavily modified headless fork of
https://github.com/BazkieBumpercar/GameplayFootball.

## Autonomous match runner

Declare both teams, pitch, match rules and startup AI configuration explicitly:

```cpp
#include "app/fixtures/default_teams.hpp" // executable-only sample inputs
#include "env/game_env.hpp"

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

### AI intent and direct interactive/replay composition

AI owns persistent desired/planned TacticalBoards and short-lived requests,
never actual simulation state. `AIConfig::initial_tactics` optionally supplies
startup boards by side; otherwise DefaultAI bootstraps them from static models.
These values are copied, not exposed as a live env mutation channel. DefaultAI
currently reads its base boards without rewriting them during Update.

Interactive/test/network/replay consumers explicitly compose `Simulation`, their
policy and optional `app/input/grf::Input` outside the headless runner. Input emits
PlayerControlSet overrides and frame-local TeamDecisionRequest values; the
composition root routes requests to its own policy and merges controls before
Simulation::Step. Direct DefaultAI tactics/Request* APIs remain for those paths,
not as GameEnv proxies. GRF wire 0–32, sticky modifiers and one-shot actions remain
value-only; input selection is not sim authority. Requests are intentions, not
facts that a kick/press/save was executed.

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

Simulation consumes values regardless of AI/human/network/replay origin, with
idle fallback for missing controls and execution-side legality. It has no policy
fallback or input ownership. Referee is the rules engine, not a moving actor.
Animation mechanics use the baked asset, never runtime XML/legacy importers.
There is no active-environment global, ScenarioConfig, GRF environment adapter,
controller hierarchy or byte-blob checkpoint API. Durable save/load would require
an explicit value state contract, not per-class memory hooks.

## Build and tests

Each module CMakeLists owns explicit sources, public FILE_SET HEADERS,
dependencies and `football::` aliases. Root CMake composes them; no `sources.cmake`
or source globbing. query/rules/player stay sim internals; sim/animation is an
independent archive also consumed by the offline baker. AI and input link only
value contracts, never sim actors. Module link/source/header ownership guards
check real target properties and have negative tests.

```sh
nix develop --command bash -c 'cmake --preset release && cmake --build --preset release -j 4 && ctest --preset release --output-on-failure'
build/release/football_app                         # one complete regulation match
build/release/football_app --half-duration-ms=1800 # complete short regulation match
```

`--steps` is retired; app always loops until sim completion and prints MatchResult,
not a partial observation. Parsing/writing belongs to app. CLI/tool executable
paths remain at the build root. `football_smoke [ticks]` is a secondary bounded
telemetry diagnostic, not the headless application's match interface.

Catch2 is fetched via CPM only with BUILD_TESTING enabled and cached in `.cache/CPM`.
`-DBUILD_TESTING=OFF -DFOOTBALL_BUILD_APP=OFF` is a network-free core-only build;
input/fixture archives remain explicit EXCLUDE_FROM_ALL targets. Neither app nor
Catch2/offline baker code is linked into core. Tests cover rule/clock completion,
results/lifecycle/restart, independent owners, AI-only and input-only value paths,
control-tape/RNG replay, baselines and architecture guards. Motion/actions/RNG
checkpoint goldens are unchanged; adding phase/time changes only World hash schema
at those checkpoints (see `test/baselines/pre_match_runner.md`). Historical strategy
and input schema hashes remain in the other files under `test/baselines/`.
