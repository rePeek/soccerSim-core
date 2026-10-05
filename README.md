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
  const WorldState world = game.observe(); // raw simulation coordinates/velocities
  game.reset_game();                       // same teams and pitch
  game.stop_game();
}
```

One `step()` calls the simulation once: a 10 ms tick (100 Hz). Batch explicitly
with a loop. `football_app --steps=100` therefore advances 100 ticks, not 100
legacy observation frames. Configure commands through `game.controls()`.

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
observations or checkpoint serialization. The GRF compatibility adapter, the
binary checkpoint layer and the `ScenarioConfig` episode input have all been
deleted, not moved to test support. Match rules are the `MatchOptions` defaults,
snapshotted by `Match`. Internal regression diagnostics own/pass `Simulation`
explicitly, while public `GameEnv` checks use only raw `WorldState` snapshots;
animation branches use deterministic reset/replay. No environment runtime
accessor or diagnostic compatibility shim remains. A future save/load feature
should serialize an explicit state value object rather than per-class byte hooks.

The headless core retains `Referee` as the football rules engine (fouls, cards,
offside and restarts), not as a moving actor. Referee/linesman humanoids are
removed; card restart deadlines are fixed rules time, never animation duration.
