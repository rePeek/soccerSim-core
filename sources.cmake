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
   src/model/player.hpp
   src/model/team.hpp
   src/model/formation.hpp
   src/model/pitch.hpp
)

# Value-only Simulation inputs/outputs; also used by policy-only consumers.
set(SIM_CONTRACT_HEADERS
   src/sim/player_control.hpp
   src/sim/player_control_set.hpp
   src/sim/world_state.hpp
   src/sim/observation_epoch.hpp
)

set(ANIMATION_HEADERS
   src/sim/animation/types.hpp
   src/sim/animation/selection_math.hpp
   src/sim/animation/selection_query.hpp
   src/sim/animation/quadrant.hpp
   src/sim/animation/simanim_format.hpp
   src/sim/animation/clip.hpp
   src/sim/animation/library.hpp
   src/sim/animation/baked_selector.hpp
)


set(ANIMATION_SOURCES
   src/sim/animation/library.cpp
   src/sim/animation/baked_selector.cpp
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
   src/sim/rng.hpp
)


set(CORE_SOURCES
   src/sim/gamedefines.cpp
)


set(GAME_HEADERS
   src/sim/player/humanoid/humanoidbase.hpp
   src/sim/player/humanoid/humanoid.hpp
   src/sim/player/humanoid/humanoid_utils.hpp
   src/sim/player/player_control_builder.hpp
   src/sim/player/player_kinematics.hpp
   src/sim/player/player_retain_anchor.hpp
   src/sim/player/player_ground_collider.hpp
   src/sim/player/player_body_collider.hpp
   src/sim/player/player_action.hpp
   src/sim/player/player_action_executor.hpp
   src/sim/player/player_action_volume.hpp
   src/sim/player/player.hpp
   src/sim/referee.hpp
   src/sim/ball.hpp
   src/sim/team.hpp
   src/sim/match_options.hpp
   src/sim/simulation.hpp
   src/sim/match.hpp
   src/sim/match_world_state.hpp
   src/sim/value_history.hpp
   src/sim/query/player_query.hpp
   src/sim/query/reachability.hpp
   src/sim/rules/offside.hpp
   src/sim/player/kick_targeting.hpp
   src/sim/ai_support/mentalimage.hpp
   src/sim/rules/restart_placement.hpp
)


set(GAME_SOURCES
   src/sim/player/humanoid/humanoid_utils.cpp
   src/sim/player/humanoid/humanoidbase.cpp
   src/sim/player/humanoid/humanoid.cpp
   src/sim/player/player.cpp
   src/sim/player/player_control_builder.cpp
   src/sim/player/player_locomotion.cpp
   src/sim/ball.cpp
   src/sim/match.cpp
   src/sim/match_world_state.cpp
   src/sim/formation.cpp
   src/sim/simulation.cpp
   src/sim/referee.cpp
   src/sim/ai_support/mentalimage.cpp
   src/sim/query/player_query.cpp
   src/sim/query/reachability.cpp
   src/sim/rules/offside.cpp
   src/sim/rules/restart_placement.cpp
   src/sim/player/kick_targeting.cpp
   src/sim/team.cpp
)


list(APPEND GAME_HEADERS src/sim/formation.hpp)

set(AI_HEADERS src/ai/default_ai.hpp src/ai/tactical_board.hpp src/ai/team_requests.hpp)
set(AI_SOURCES src/ai/default_ai.cpp src/ai/tactical_board.cpp)

# Executable-side code. It builds model descriptions from legacy data, so it may
# use support (XML/text) but is never linked into the core shared library.
set(APP_SUPPORT_HEADERS
   src/app/args.hpp
   src/app/fixtures/legacy_player_profile.hpp
   src/app/fixtures/default_teams.hpp
)

set(APP_SUPPORT_SOURCES
   src/app/args.cpp
   src/app/fixtures/legacy_player_profile.cpp
   src/app/fixtures/default_teams.cpp
)

set(APP_INPUT_HEADERS src/app/input/grf/action.hpp src/app/input/grf/input.hpp)
set(APP_INPUT_SOURCES src/app/input/grf/input.cpp)

set(APP_MAIN_SOURCES
   src/app/app.cpp
)