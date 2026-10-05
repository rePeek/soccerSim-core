# Gameplay Football
This is a heavily modified version of
https://github.com/BazkieBumpercar/GameplayFootball repository.

## Core API

Declare both teams and the pitch explicitly; `GameEnv` has no default constructor.

```cpp
#include "data/default_teams.hpp"
#include "env/game_env.hpp"

int main() {
  GameEnv game{football::data::MakeDefaultHomeTeam(),
               football::data::MakeDefaultAwayTeam(),
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

`GameEnv` does not expose runtime containers, episode configuration, GRF
observations or checkpoint serialization. The GRF compatibility adapter, the
binary checkpoint layer and the `ScenarioConfig` episode input have all been
deleted, not moved to test support. Match rules are the `MatchOptions` defaults,
snapshotted by `Match`; regression uses the core API and raw `WorldState`
snapshots, and animation branches use deterministic reset/replay. A future
save/load feature should serialize an explicit state value object rather than
per-class byte hooks.
