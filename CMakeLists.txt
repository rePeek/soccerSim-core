cmake_minimum_required(VERSION 3.24)

project(football LANGUAGES CXX)

include(CTest)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CPM.cmake)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/dependencies.cmake)

add_subdirectory(src/core)

add_executable(footballSim src/app/main.cpp)
target_link_libraries(footballSim PRIVATE footballCore nlohmann_json::nlohmann_json)

if(BUILD_TESTING)
  add_executable(footballCoreTests tests/football_core_tests.cpp)
  target_link_libraries(footballCoreTests PRIVATE footballCore Catch2::Catch2WithMain)
  add_test(NAME footballCoreTests COMMAND footballCoreTests)
endif()

# Legacy onthepitch / engine / graphics sources remain in the tree for
# reference, but are deliberately absent from the headless build.
