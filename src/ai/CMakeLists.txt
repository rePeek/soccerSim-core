# Startup values used by the game facade without exposing the concrete policy.
add_library(football_ai_contracts INTERFACE)
add_library(football::ai_contracts ALIAS football_ai_contracts)
target_sources(football_ai_contracts INTERFACE FILE_SET HEADERS
  BASE_DIRS ${PROJECT_SOURCE_DIR}/include
  FILES ${PROJECT_SOURCE_DIR}/include/football/ai/ai_config.hpp ${PROJECT_SOURCE_DIR}/include/football/ai/tactical_board.hpp)
target_compile_features(football_ai_contracts INTERFACE cxx_std_23)
target_link_libraries(football_ai_contracts INTERFACE football::foundation football::pitch)

# Value policy: no actor/runtime link or input ownership.
add_library(football_ai STATIC)
add_library(football::ai ALIAS football_ai)
target_sources(football_ai
  PRIVATE default_ai.cpp tactical_board.cpp
  PUBLIC FILE_SET HEADERS
    BASE_DIRS ${PROJECT_SOURCE_DIR}/include
    FILES ${PROJECT_SOURCE_DIR}/include/football/ai/default_ai.hpp)
target_link_libraries(football_ai PUBLIC football::sim_contracts football::ai_contracts)
football_configure_target(football_ai)
