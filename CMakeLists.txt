cmake_minimum_required(VERSION 3.24)

project(football LANGUAGES CXX)

include(CTest)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CPM.cmake)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(footballCore STATIC
  src/base/log.cpp
  src/cmake/backtrace.cpp
  src/foundation/math/vector3.cpp
  src/foundation/math/matrix3.cpp
  src/foundation/math/matrix4.cpp
  src/foundation/math/quaternion.cpp
  src/foundation/math/bluntmath.cpp
  src/core/model/ball/ball.cpp
  src/core/model/player/player.cpp
  src/core/physics/physics_system.cpp
  src/core/physics/player_movement.cpp
)
target_include_directories(footballCore PUBLIC
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/cmake
)
target_compile_features(footballCore PUBLIC cxx_std_23)

# JSON is an application concern: footballCore remains dependency-free.
CPMAddPackage(
  NAME nlohmann_json
  GITHUB_REPOSITORY nlohmann/json
  GIT_TAG v3.11.3
  OPTIONS "JSON_BuildTests OFF"
)

add_executable(footballSim src/app/main.cpp)
target_link_libraries(footballSim PRIVATE
  footballCore
  nlohmann_json::nlohmann_json
)
target_compile_features(footballSim PRIVATE cxx_std_23)

if(BUILD_TESTING)
  CPMAddPackage(
    NAME Catch2
    GITHUB_REPOSITORY catchorg/Catch2
    GIT_TAG v3.7.1
    OPTIONS "CATCH_BUILD_TESTING OFF" "CATCH_INSTALL_DOCS OFF"
  )

  add_executable(footballCoreTests tests/football_core_tests.cpp)
  target_link_libraries(footballCoreTests PRIVATE footballCore Catch2::Catch2WithMain)
  target_compile_features(footballCoreTests PRIVATE cxx_std_23)
  add_test(NAME footballCoreTests COMMAND footballCoreTests)
endif()

# The legacy onthepitch / engine / graphics sources remain in the repository
# for reference, but are deliberately absent from this headless build.
