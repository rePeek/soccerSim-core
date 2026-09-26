cmake_minimum_required(VERSION 3.4)

set(OUTPUT_LIB_NAME game)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/CMakeModules")

project(gameplayfootball)
include(CTest)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "This project supports Linux only")
endif()
set(PACKAGE gameplayfootball)

set(CMAKE_CXX_STANDARD 14)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${GCC_COVERAGE_COMPILE_FLAGS} -fPIC -g -O3")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC -O3 -g")
set(OpenGL_GL_PREFERENCE GLVND)

# Find required libraries
FIND_PACKAGE(OpenGL REQUIRED)
include_directories(${OPENGL_INCLUDE_DIR})

FIND_PACKAGE(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})

FIND_PACKAGE(EGL REQUIRED)
include_directories(${EGL_INCLUDE_DIR})

FIND_PACKAGE(SDL2_image REQUIRED)
FIND_PACKAGE(SDL2_gfx REQUIRED)

include_directories(${SDL2_IMAGE_DIRS})
include_directories(${SDL2_GFX_DIRS})

FIND_PACKAGE(Boost REQUIRED COMPONENTS thread system filesystem)
include_directories(${Boost_INCLUDE_DIRS})

include_directories(${PROJECT_SOURCE_DIR}/src)
include_directories(${PROJECT_SOURCE_DIR}/src/cmake)

# Include the sources
include(sources.cmake)

add_library(baselib OBJECT ${BASE_SOURCES} ${BASE_HEADERS}
   ${BASE_GEOMETRY_HEADERS} ${BASE_MATH_HEADERS})
add_library(loaderslib OBJECT ${LOADERS_SOURCES} ${LOADERS_HEADERS} ${MANAGERS_HEADERS})
add_library(typeslib OBJECT ${TYPES_SOURCES} ${TYPES_HEADERS})
add_library(scenelib OBJECT ${SCENE_SOURCES} ${SCENE_HEADERS}
   ${SCENE2D_HEADERS} ${SCENE3D_HEADERS} ${SCENE_OBJECTS_HEADERS}
   ${SCENE_RESOURCES_HEADERS})
add_library(utilslib OBJECT ${UTILS_SOURCES} ${UTILS_HEADERS}
   ${UTILS_EXT_HEADERS})

# Join all created static libraries into a single static or shared one.
set(OWN_LIBRARIES $<TARGET_OBJECTS:baselib> # $<TARGET_OBJECTS:systemscommonlib>
   $<TARGET_OBJECTS:loaderslib>
   $<TARGET_OBJECTS:typeslib> $<TARGET_OBJECTS:scenelib>
   $<TARGET_OBJECTS:utilslib>)

# Compile it as multiple static libraries
add_library(blunted2 ${BLUNTED_CORE_SOURCES} ${BLUNTED_CORE_HEADERS} ${OWN_LIBRARIES})
add_library(gamelib ${GAME_SOURCES} ${GAME_HEADERS})
add_library(datalib ${DATA_SOURCES} ${DATA_HEADERS})

set(SDL2_LIBS_ALL ${SDL2_IMAGE_LIBRARIES} ${SDL2_GFX_LIBRARIES}
   ${SDL2_LIBRARIES})

set(ENGINE_LIBRARIES gamelib datalib blunted2
   Boost::filesystem Boost::system Boost::thread
   ${SDL2_LIBS_ALL} ${EGL_LIBRARIES} ${OPENGL_LIBRARIES})

add_library(enginecore OBJECT ${CORE_SOURCES} ${CORE_HEADERS}
   src/game_env.cpp src/game_env.hpp
   src/ai/ai_keyboard.cpp src/ai/ai_keyboard.hpp)

add_library(${OUTPUT_LIB_NAME} SHARED $<TARGET_OBJECTS:enginecore>)
target_link_libraries(${OUTPUT_LIB_NAME} PRIVATE ${ENGINE_LIBRARIES})

# Minimal C++ smoke test: exercises the C++ engine directly.
add_executable(football_smoke tools/football_smoke.cpp)
target_link_libraries(football_smoke PRIVATE ${OUTPUT_LIB_NAME})

if(BUILD_TESTING)
  add_executable(football_regression tools/football_regression.cpp)
  target_link_libraries(football_regression PRIVATE ${OUTPUT_LIB_NAME})
  add_test(NAME football_regression COMMAND football_regression)
  set_tests_properties(football_regression PROPERTIES
    ENVIRONMENT "GFOOTBALL_DATA_DIR=${CMAKE_SOURCE_DIR}/data")
endif()
