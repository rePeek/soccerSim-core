# Gameplay Football
This is a heavily modified version of
https://github.com/BazkieBumpercar/GameplayFootball repository.

## Core API

Declare both teams and the pitch explicitly; `GameEnv` has no default constructor.
The example below uses the CLI's sample inputs, which live in `src/app/fixtures/`
and are linked only into executables, never into the core shared library.

```cpp
#include "app/fixtures/default_teams.hpp"
#include "env/game_env.hpp"

int main() {
  GameEnv game{football::app::fixtures::MakeDefaultHomeTeam(),
               football::app::fixtures::MakeDefaultAwayTeam(),
               football::model::MakeLegacyPitch()};
  game.start_game();
  for (int tick = 0; tick < 100; ++tick) game.step();
  const WorldState world = game.observe(); // owning, unscaled home-pitch values
  game.reset_game();                       // same teams and pitch
  game.stop_game();
}
```

One `step()` calls the simulation once: a 10 ms tick (100 Hz). Batch explicitly
with a loop. `football_app --steps=100` therefore advances 100 ticks, not 100
legacy observation frames. Configure commands through `game.controls()`.

Default AI remains shipped in `libgame.so`, but runs outside simulation:
`WorldState + TacticalBoard → DefaultAI → PlayerControlSet → Simulation`.
Explicit controls override default decisions; simulation has no input ownership or Human path.
Simulation itself has no decision objects/factories and no implicit AI fallback.
AI links only the header-only `football_sim_contracts` target, never actor/runtime
code. Simulation owns its input/output headers: `sim/player_control.hpp`,
`sim/player_control_set.hpp`, and `sim/world_state.hpp`. Exact-header dependency
guards prevent AI from including other `sim/` headers. Snapshots include ball
motion, play/restart/retention state and both team states; controls and observations
use a common home pitch frame.

`DefaultAI(home, away, pitch)` builds its persistent tactical boards directly from
static `model::Team` declarations, with no simulation bootstrap. Edit them through
`game.tactics(TeamSide::Home)`; width/depth are pitch fractions (defaults 0.75/0.55).
Player AI derives local targets without rewriting the base plan. Simulation
reset/stop/start preserves tactical configuration.
Run/pressure/rush requests live separately in `ai::TeamRequests`, not in the board
or `WorldState`. They expire on observed ticks or actual world reset sequences.
Start/reset/stop clears these short-lived requests, never the persistent tactics.

### GRF / human input

Only seven top-level source modules remain: foundation, support, model, sim, ai,
env and app. The retired controller/control/observation/data layers are deleted.
`src/app/input/grf/` owns wire actions, sticky state and selected IDs. Link the
value-only `football_app_input` archive alongside the core; it never links actors.

```cpp
#include "app/input/grf/input.hpp"

// home/away/pitch are the same static descriptions supplied to GameEnv.
GameEnv game(home, away, pitch);
football::app::grf::Input input(home, football::model::TeamSide::Home);
game.start_game();
input.Apply(football::app::grf::Action::Right);
for (int tick = 0; tick < 100; ++tick) {
  input.Update(game.observe(), game.default_ai(), game.controls());
  game.step();  // AI defaults, then explicit input overrides, then one sim tick
}
```

Wire numbers 0–32 are retained. Movement/modifiers/pressure/rush are sticky;
kicks/sliding/switch are one-shot requests with default kick power 0.6, not the old
Human animation planner/gauge. Requests remain subject to sim cadence/legality,
not proof of execution. BuiltinAI relinquishes overrides. Reset input
state explicitly when starting a new session. Multiple input slots reserve IDs
in the application, never in simulation. Record/replay the resulting
`PlayerControlSet` frames without any input or AI objects.

The old Eliza strategy was intentionally replaced, not wrapped. Current policy
goldens differ; historical values are recorded in
[`test/baselines/pre_value_ai.md`](test/baselines/pre_value_ai.md). Rules, restart
placement/RNG order, actor reset lifetimes and animation mechanics are separately
verified.

`GameEnv` directly owns its `Simulation`; there is no active-environment global
or context binding. Reset retains the runtime RNG and animation cache; stop
releases the runtime, and restart constructs it afresh. Controls clear on
start/reset/stop. Rejected startup leaves the environment stopped.

The core accepts already-constructed domain objects. It does not know default
teams, legacy database ids, profile files or where data lives: `src/data/` is
deleted. `src/app/fixtures/default_teams.*` and
`src/app/fixtures/legacy_player_profile.*` translate legacy defaults/profiles into
`model::Team`/`model::Player` for the CLI and tests only.

Every description must provide a valid `model::Player::id`, unique across both
rosters, and a roster not smaller than its effective formation. There is no
implicit default roster, so an empty `model::Team` is rejected. `PlayerId` lives
in `model/player.hpp`; `database_id` is only legacy profile provenance and never
supplies identity. Controls and observations use those IDs unchanged. The sample
factories supply disjoint IDs 0–10 and 11–21. Invalid/duplicate IDs and empty
rosters are rejected at startup before simulation RNG draws.

`WorldPlayerState::side` is `model::TeamSide::Home` or `Away`, not a stable club
ID. There is no second runtime player identity: `Player::GetID()` reads the
owned model ID directly. Roster order comes from containers; a private repeating
`schedule_phase_` only staggers calculations, never keys controls or snapshots.

`GameEnv` does not expose runtime containers, episode configuration, GRF
observations or checkpoint serialization. The GRF environment adapter (distinct
from the app action decoder), binary checkpoint layer and ScenarioConfig episode
input remain deleted. Match rules are the MatchOptions defaults,
snapshotted by `Match`. Internal regression diagnostics own/pass `Simulation`
explicitly, while public `GameEnv` checks use only raw `WorldState` snapshots;
animation branches use deterministic reset/replay. No environment runtime
accessor or diagnostic compatibility shim remains. A future save/load feature
should serialize an explicit state value object rather than per-class byte hooks.

The headless core retains `Referee` as the football rules engine (fouls, cards,
offside and restarts), not as a moving actor. Referee/linesman humanoids are
removed; card restart deadlines are fixed rules time, never animation duration.

## Tests

`ctest --preset release` runs the core regression/diagnostic executables plus the
Catch2 suites in `test/`: value-only AI/requests and GRF input, controls/snapshots/rules,
exact restart/computation baselines, plain control-tape replay, fixtures, CLI and
binary output. Architecture guards have negative tests too.
Catch2 is pulled in by
[CPM](https://github.com/cpm-cmake/CPM.cmake) (`cmake/CPM.cmake`) and cached in
`.cache/CPM`; nothing in `test/`, `src/app/` or Catch2 is linked into the core
shared library. Configure with `-DBUILD_TESTING=OFF` for a network-free,
dependency-free build, and `-DFOOTBALL_BUILD_APP=OFF` to skip the CLI as well.
