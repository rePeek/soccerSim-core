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

set(BASE_HEADERS
   src/foundation/defines.hpp
   src/foundation/log.hpp
   src/foundation/utils.hpp
   src/foundation/properties.hpp
   src/foundation/xml_loader.hpp
)


set(BASE_GEOMETRY_HEADERS
   src/foundation/geometry/aabb.hpp
   src/foundation/geometry/trianglemeshutils.hpp
   src/foundation/geometry/plane.hpp
   src/foundation/geometry/triangle.hpp
   src/foundation/geometry/line.hpp
)


set(BASE_MATH_HEADERS
   src/foundation/math/quaternion.hpp
   src/foundation/math/matrix3.hpp
   src/foundation/math/matrix4.hpp
   src/foundation/math/vector3.hpp
   src/foundation/math/bluntmath.hpp
)


set(BASE_SOURCES
   src/foundation/utils.cpp
   src/foundation/properties.cpp
   src/foundation/log.cpp
   src/foundation/backtrace.cpp
   src/foundation/file.cpp
   src/foundation/misc/hungarian.cpp
   src/foundation/geometry/triangle.cpp
   src/foundation/geometry/line.cpp
   src/foundation/geometry/trianglemeshutils.cpp
   src/foundation/geometry/aabb.cpp
   src/foundation/geometry/plane.cpp
   src/foundation/math/vector3.cpp
   src/foundation/math/matrix3.cpp
   src/foundation/math/bluntmath.cpp
   src/foundation/math/rng.cpp
   src/foundation/math/quaternion.cpp
   src/foundation/math/matrix4.cpp
   src/foundation/xml_loader.cpp
)


set(TYPES_HEADERS
   src/foundation/types/refcounted.hpp
   src/foundation/types/command.hpp
)


set(TYPES_SOURCES
   src/foundation/types/refcounted.cpp
   src/foundation/types/command.cpp
)


set(UTILS_HEADERS
   src/animation/types.hpp
   src/animation/selection_math.hpp
   src/animation/selection_query.hpp
   src/animation/quadrant.hpp
   src/animation/simanim_format.hpp
   src/animation/clip.hpp
   src/animation/library.hpp
   src/animation/baked_selector.hpp
)


set(UTILS_EXT_HEADERS
   src/animation/extensions/animationextension.hpp
   src/animation/extensions/footballanimationextension.hpp
)


set(UTILS_SOURCES
   src/animation/library.cpp
   src/animation/baked_selector.cpp
)

set(LEGACY_ANIM_SOURCES
   src/animation/animation.cpp
   src/animation/animcollection.cpp
   src/animation/import_hierarchy.cpp
   src/animation/import_loader.cpp
   src/animation/extensions/footballanimationextension.cpp
)
set(BLUNTED_CORE_HEADERS
   src/env/defines.hpp
)


set(BLUNTED_CORE_SOURCES
)


set(CORE_HEADERS
   src/sim/gamedefines.hpp
   src/sim/utils.hpp
   src/env/main.hpp
   src/env/gametask.hpp
   src/env/match_setup.hpp
)


set(CORE_SOURCES
   src/env/gametask.cpp
   src/sim/utils.cpp
   src/env/main.cpp
   src/sim/gamedefines.cpp
   src/env/defines.cpp
)


set(GAME_HEADERS
   src/sim/humangamer.hpp
   src/sim/officials.hpp
   src/sim/player/humanoid/humanoidbase.hpp
   src/sim/player/humanoid/humanoid.hpp
   src/sim/player/humanoid/humanoid_utils.hpp
   src/sim/player/playerofficial.hpp
   src/sim/player/playerbase.hpp
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
   src/sim/match.hpp
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