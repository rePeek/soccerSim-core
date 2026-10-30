# Gameplay Football

A heavily modified headless fork of
https://github.com/BazkieBumpercar/GameplayFootball.

`soccerSim-core` is a deterministic, RNG-free, 100 Hz football simulation in
C++23. It is the engine only: no rendering, no input protocol, no network.

## Autonomous match runner

Declare both teams, pitch, match rules and startup AI configuration explicitly:

```cpp
#include "app/fixtures/default_teams.hpp" // executable-only sample inputs
#include "football/env.hpp"

GameEnv game{football::app::fixtures::MakeDefaultHomeTeam(),
             football::app::fixtures::MakeDefaultAwayTeam(),
             football::pitch::Pitch{}, football::ball::BallModel{},
             MatchOptions{}, football::ai::AIConfig{}};
game.Start();
while (!game.Finished()) game.Step();
const MatchResult result = game.Result();
```

`GameEnv` owns one Simulation and one AI composition root (tactical boards and
the coach loop). Until the Intent Runtime lands there is no player-decision
policy, so `AI::Update` emits no control and Player executes its neutral idle
state. Its running step is only:

```text
Simulation::Observe -> AI::Update -> PlayerControlSet -> Simulation::Step
```

It does not implement football rules, tactical reasoning, coach scheduling,
configuration parsing, output serialization or rendering. The public API is
`Start`, `Step`, `Finished`, `Result`, `Observe`, `Stop`; read-only telemetry
provides snapshots, snapshot windows, metadata and event trajectories as owning
values. Start requires a stopped runner; Stop is idempotent; after full time
`Step` is a no-op and a stopped `Step`/`Observe` throws `std::logic_error`.

## Phases, clocks and final results

The referee/sim owns `MatchPhase::{PreMatch, FirstHalf, SecondHalf, Finished}`.
Simulation owns three clocks:

- `WorldState::tick`: monotonic simulation timeline, one tick = 10 ms.
- `regulation_time` (`TickSpan`): runs after the half's actual kickoff,
  including ordinary dead balls; stops at half and full time.
- `ball_in_play_time` (`TickSpan`): accumulates only live-ball ticks.

`half_underway` and `ball_in_play` project these rule facts. `in_play` is
execution authorization: a Ready taker can act before actual contact makes the
ball live. Snapshots are observations, never authoritative storage.

`MatchOptions::half_duration` is a `TickSpan` (two halves, no added or extra
time). Invalid durations/capacities are rejected before any actor is built.
Half time flips each team's defending direction at the next between-tick
boundary; it never rewrites stored positions. `MatchResult` is available only
after full time.

## Coordinate frame

There is exactly one physical coordinate system, the **Stadium Frame**: x is
pitch length, y is width, z is up, the centre spot is the origin. Axes never
mirror. Home defends -X and Away +X in the first half; the change of ends only
flips which end each team defends. Ball, players, colliders, mental images,
snapshots, `WorldState` and controls all share this frame.

Each team has a `TeamOrientation` with a single `defending_direction`; the
attack direction is its negation. Team-relative interpretation is always an
explicit direction value, and stored physics is never mirrored. Pitch owns
geometry and the static ground/post/crossbar scene only.

## Determinism

The simulation core contains no random number generator. Player motion,
animation choice, touches and set-piece placement are deterministic functions of
skill, contact geometry, body orientation, ball velocity and fatigue. Given the
same declarations and `PlayerControlSet` tape, Simulation produces the same
trajectory without an AI or a seed.

## Simulation domains

```text
src/                (runtime implementation; public values live in include/football/)
├── foundation/   math/time, geometry, generic collision
├── animation/    baked clip schema/library/selector, playback, touch, pose
├── pitch/        Pitch description and static physics scene
├── ball/         Ball physics: impulse/response, integration, prediction
├── player/       four libs: state, command, contact, execution
├── team/         Static Team entity, lineup/orders/presets, Player ownership
├── runtime/      Simulation, clocks/phases/result, TeamOrientation/TeamView,
│                 possession, restart context/controller/planner/placement,
│                 WorldState and Snapshot builders
├── event/        EventRecognizer, TouchState, MatchEvent log
├── observation/  MentalImage, Snapshot/history, temporal graph, player slots
├── referee/      Referee over value facts, goal/period/offside/restart rules
├── ai/           tactical board/plan/interpreter, coach
└── testing/      SimulationAccess (internal, never product/actor-facing)
```

Dependency direction is one-way:
`foundation -> geometry/collision -> pitch -> ball/animation -> player -> team
-> runtime`, with `ai`, `model`, `referee`, `event` and `observation` depending
only on public value contracts. AI consumes
`football/observation/world_state.hpp` and
`football/player/player_control_set.hpp`; apps use `football/env.hpp`.
`WorldState` is an owning observation projection, never mutable world authority.

## Animation and character assets

Canonical animation sources are GLBs and football metadata under
`assets/animations/`; appearance GLBs (complete default body plus swappable
base/kit/shoes/hair slots) live in `assets/characters/`, with one shared skeleton
in `assets/skeletons/player.json`.
See [assets/README.md](assets/README.md) for the authoring contract.

CMake uses Python 3.10+ (included in the Nix shell) to compile the GLB catalog
into `<build-dir>/assets/runtime/animations.simanim`. This is a generated cache,
not a checked-in asset. Normal builds need neither Blender nor the old `.anim`
baker. Blender preview tools are documented in
[tools/playerAssets/README.md](tools/playerAssets/README.md).

## Build and tests

```sh
nix develop --command bash -c 'cmake --preset debug && cmake --build --preset debug -j $(nproc) && ctest --preset debug'
nix develop --command bash -c 'cmake -S . -B build/core -DBUILD_TESTING=OFF && cmake --build build/core -j $(nproc)'
```

Each module CMakeLists owns explicit sources, public FILE_SET HEADERS,
dependencies and `football::` aliases. `football::simcore` compiles
`src/env.cpp` and publishes `include/football/env.hpp` as the public facade.

BqLog and nlohmann/json are fetched via CPM in every build mode; Catch2 is
fetched only with `BUILD_TESTING=ON`. All are cached in `.cache/CPM`.

Unit tests live under `unittests/` and link only the module under test. The
current scope is `pitch`, `ball`, `player` and `team`, plus the `animation`
acceptance suites and the Player/Team link smokes; no test constructs a full
Simulation or plays a match. Tests assert physical correctness, state
invariants and module contracts; there is no golden/fingerprint tier and no
full-match requirement for a module change. The old `.anim` baker under
`tools/animBaker/` is an explicitly selected recovery tool, excluded from the
default build. Asset compiler tests are included in the `animation` label.
