add_library(footballCore STATIC
  math/vector3.cpp
  math/matrix3.cpp
  math/matrix4.cpp
  math/quaternion.cpp
  math/math.cpp
  model/ball/ball.cpp
  model/player/player.cpp
  physics/physics_system.cpp
  physics/movement/player/player_movement.cpp
)
target_include_directories(footballCore PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(footballCore PRIVATE spdlog::spdlog)
