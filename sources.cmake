# Copyright 2019 Google LLC
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Football-domain value types. No project dependency of any kind: this is the
# leaf of the source DAG. Header-only, so it is registered here but compiled
# into whichever target includes it.
set(MODEL_HEADERS
   src/model/football_types.hpp
)






set(ANIMATION_HEADERS
   src/animation/types.hpp
   src/animation/selection_math.hpp
   src/animation/selection_query.hpp
   src/animation/quadrant.hpp
   src/animation/simanim_format.hpp
   src/animation/clip.hpp
   src/animation/library.hpp
   src/animation/baked_selector.hpp
)


set(ANIMATION_SOURCES
   src/animation/library.cpp
   src/animation/baked_selector.cpp
)

set(LEGACY_ANIM_SOURCES
   tools/animBaker/animation/animation.cpp
   tools/animBaker/animation/animcollection.cpp
   tools/animBaker/animation/import_hierarchy.cpp
   tools/animBaker/animation/import_loader.cpp
   tools/animBaker/animation/extensions/footballanimationextension.cpp
)


set(CORE_HEADERS
   src/sim/gamedefines.hpp
   src/sim/utils.hpp
   src/sim/rng.hpp
   src/env/main.hpp
   src/env/rng.hpp
   src/env/defines.hpp
   src/env/match_setup.hpp
)


set(CORE_SOURCES
   src/sim/utils.cpp
   src/env/main.cpp
   src/sim/gamedefines.cpp
   src/env/defines.cpp
   src/env/rng.cpp
   src/env/match_setup.cpp
)


set(GAME_HEADERS
   src/sim/humangamer.hpp
   src/sim/officials.hpp
   src/sim/player/humanoid/humanoidbase.hpp
   src/sim/player/humanoid/humanoid.hpp
   src/sim/player/humanoid/humanoid_utils.hpp
   src/sim/player/playerofficial.hpp
   src/sim/player/playerbase.hpp
   src/sim/player/player_control_builder.hpp
   src/sim/player/player_kinematics.hpp
   src/sim/player/player_retain_anchor.hpp
   src/sim/player/player_ground_collider.hpp
   src/sim/player/player_body_collider.hpp
   src/sim/player/player_action.hpp
   src/sim/player/player_action_executor.hpp
   src/sim/player/player_action_volume.hpp
   src/sim/player/player.hpp
   src/sim/player/controller/icontroller.hpp
   src/sim/player/controller/elizacontroller.hpp
   src/sim/player/controller/humancontroller.hpp
   src/sim/player/controller/playercontroller.hpp
   src/sim/player/controller/strategies/strategy.hpp
   src/sim/player/controller/strategies/offtheball/default_off.hpp
   src/sim/player/controller/strategies/offtheball/default_def.hpp
   src/sim/player/controller/strategies/offtheball/default_mid.hpp
   src/sim/player/controller/strategies/offtheball/goalie_default.hpp
   src/sim/player/controller/refereecontroller.hpp
   src/sim/referee.hpp
   src/sim/ball.hpp
   src/sim/team.hpp
   src/sim/match_config.hpp
   src/sim/pitch.hpp
   src/sim/simulation.hpp
   src/sim/match.hpp
   src/sim/match_world_state.hpp
   src/sim/value_history.hpp
   src/sim/ai_support/AIfunctions.hpp
   src/sim/ai_support/mentalimage.hpp
   src/sim/teamAIcontroller.hpp
)


set(GAME_SOURCES
   src/sim/officials.cpp
   src/sim/player/humanoid/humanoid_utils.cpp
   src/sim/player/humanoid/humanoidbase.cpp
   src/sim/player/humanoid/humanoid.cpp
   src/sim/player/playerofficial.cpp
   src/sim/player/player.cpp
   src/sim/player/playerbase.cpp
   src/sim/player/player_control_builder.cpp
   src/sim/player/player_locomotion.cpp
   src/sim/player/controller/playercontroller.cpp
   src/sim/player/controller/humancontroller.cpp
   src/sim/player/controller/icontroller.cpp
   src/sim/player/controller/refereecontroller.cpp
   src/sim/player/controller/elizacontroller.cpp
   src/sim/player/controller/strategies/strategy.cpp
   src/sim/player/controller/strategies/offtheball/default_mid.cpp
   src/sim/player/controller/strategies/offtheball/default_off.cpp
   src/sim/player/controller/strategies/offtheball/default_def.cpp
   src/sim/player/controller/strategies/offtheball/goalie_default.cpp
   src/sim/humangamer.cpp
   src/sim/ball.cpp
   src/sim/match.cpp
   src/sim/match_world_state.cpp
   src/sim/simulation.cpp
   src/sim/referee.cpp
   src/sim/ai_support/mentalimage.cpp
   src/sim/ai_support/AIfunctions.cpp
   src/sim/team.cpp
   src/sim/teamAIcontroller.cpp
)


set(DATA_HEADERS
   src/data/matchdata.hpp
   src/data/teamdata.hpp
   src/data/playerdata.hpp
)


set(DATA_SOURCES
   src/data/matchdata.cpp
   src/data/playerdata.cpp
   src/data/teamdata.cpp
)