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
   src/base/log.hpp
   src/base/utils.hpp
   src/base/properties.hpp
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
   src/base/utils.cpp
   src/base/properties.cpp
   src/base/log.cpp
   src/foundation/geometry/triangle.cpp
   src/foundation/geometry/line.cpp
   src/foundation/geometry/trianglemeshutils.cpp
   src/foundation/geometry/aabb.cpp
   src/foundation/geometry/plane.cpp
   src/foundation/math/vector3.cpp
   src/foundation/math/matrix3.cpp
   src/foundation/math/bluntmath.cpp
   src/foundation/math/quaternion.cpp
   src/foundation/math/matrix4.cpp
)


set(TYPES_HEADERS
   src/types/refcounted.hpp
   src/types/command.hpp
)

set(TYPES_SOURCES
   src/types/refcounted.cpp
   src/types/command.cpp
)


set(UTILS_HEADERS
   src/utils/animation.hpp
   src/utils/xmlloader.hpp
)

set(UTILS_EXT_HEADERS
   src/utils/animationextensions/animationextension.hpp
   src/utils/animationextensions/footballanimationextension.hpp
)

set(UTILS_SOURCES
   src/utils/animation.cpp
   src/utils/xmlloader.cpp
   src/utils/animationextensions/footballanimationextension.cpp
)


set(BLUNTED_CORE_HEADERS
   src/defines.hpp
)

set(BLUNTED_CORE_SOURCES
)



set(CORE_HEADERS
   src/cmake/backtrace.h
   src/cmake/file.h
   src/gamedefines.hpp
   src/utils.hpp
   src/main.hpp
   src/gametask.hpp
   src/match_setup.hpp
   src/misc/hungarian.h
   src/core/world/world.hpp
   src/core/state/ball_state.hpp
)

set(CORE_SOURCES
   src/cmake/backtrace.cpp
   src/cmake/file.cpp
   src/misc/perlin.cpp
   src/misc/hungarian.cpp
   src/gametask.cpp
   src/core/world/world.cpp
   src/utils.cpp
   src/main.cpp
   src/gamedefines.cpp
   src/defines.cpp
)

set(GAME_HEADERS
   src/onthepitch/humangamer.hpp
   src/onthepitch/officials.hpp
   src/onthepitch/player/humanoid/humanoidbase.hpp
   src/onthepitch/player/humanoid/humanoid.hpp
   src/onthepitch/player/humanoid/import_hierarchy.hpp
   src/onthepitch/player/humanoid/import_loader.hpp
   src/onthepitch/player/humanoid/animcollection.hpp
   src/onthepitch/player/humanoid/humanoid_utils.hpp
   src/onthepitch/player/playerofficial.hpp
   src/onthepitch/player/playerbase.hpp
   src/onthepitch/player/player_kinematics.hpp
   src/onthepitch/player/player_retain_anchor.hpp
   src/onthepitch/player/player_ground_collider.hpp
   src/onthepitch/player/player_body_collider.hpp
   src/onthepitch/player/player_action.hpp
   src/onthepitch/player/player_action_executor.hpp
   src/onthepitch/player/player_action_volume.hpp
   src/onthepitch/player/player.hpp
   src/onthepitch/player/controller/icontroller.hpp
   src/onthepitch/player/controller/elizacontroller.hpp
   src/onthepitch/player/controller/humancontroller.hpp
   src/onthepitch/player/controller/playercontroller.hpp
   src/onthepitch/player/controller/strategies/strategy.hpp
   src/onthepitch/player/controller/strategies/offtheball/default_off.hpp
   src/onthepitch/player/controller/strategies/offtheball/default_def.hpp
   src/onthepitch/player/controller/strategies/offtheball/default_mid.hpp
   src/onthepitch/player/controller/strategies/offtheball/goalie_default.hpp
   src/onthepitch/player/controller/refereecontroller.hpp
   src/onthepitch/referee.hpp
   src/onthepitch/ball.hpp
   src/onthepitch/team.hpp
   src/onthepitch/match.hpp
   src/onthepitch/AIsupport/AIfunctions.hpp
   src/onthepitch/AIsupport/mentalimage.hpp
   src/onthepitch/teamAIcontroller.hpp
)

set(GAME_SOURCES
   src/onthepitch/officials.cpp
   src/onthepitch/player/humanoid/humanoid_utils.cpp
   src/onthepitch/player/humanoid/import_hierarchy.cpp
   src/onthepitch/player/humanoid/import_loader.cpp
   src/onthepitch/player/humanoid/animcollection.cpp
   src/onthepitch/player/humanoid/humanoidbase.cpp
   src/onthepitch/player/humanoid/humanoid.cpp
   src/onthepitch/player/playerofficial.cpp
   src/onthepitch/player/player.cpp
   src/onthepitch/player/playerbase.cpp
   src/onthepitch/player/player_locomotion.cpp
   src/onthepitch/player/controller/playercontroller.cpp
   src/onthepitch/player/controller/humancontroller.cpp
   src/onthepitch/player/controller/icontroller.cpp
   src/onthepitch/player/controller/refereecontroller.cpp
   src/onthepitch/player/controller/elizacontroller.cpp
   src/onthepitch/player/controller/strategies/strategy.cpp
   src/onthepitch/player/controller/strategies/offtheball/default_mid.cpp
   src/onthepitch/player/controller/strategies/offtheball/default_off.cpp
   src/onthepitch/player/controller/strategies/offtheball/default_def.cpp
   src/onthepitch/player/controller/strategies/offtheball/goalie_default.cpp
   src/onthepitch/humangamer.cpp
   src/onthepitch/ball.cpp
   src/onthepitch/match.cpp
   src/onthepitch/referee.cpp
   src/onthepitch/AIsupport/mentalimage.cpp
   src/onthepitch/AIsupport/AIfunctions.cpp
   src/onthepitch/team.cpp
   src/onthepitch/teamAIcontroller.cpp
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
