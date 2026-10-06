# Startup values used by the game facade without exposing the concrete policy.
add_library(football_ai_contracts INTERFACE)
add_library(football::ai_contracts ALIAS football_ai_contracts)
target_sources(football_ai_contracts INTERFACE FILE_SET HEADERS
  BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/..
  FILES ai_config.hpp tactical_board.hpp)
target_compile_features(football_ai_contracts INTERFACE cxx_std_23)
target_link_libraries(football_ai_contracts INTERFACE football::model football::foundation)

# Value policy: no actor/runtime link or input ownership.
add_library(football_ai STATIC)
add_library(football::ai ALIAS football_ai)
target_sources(football_ai
  PRIVATE default_ai.cpp tactical_board.cpp
  PUBLIC FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/..
    FILES default_ai.hpp team_requests.hpp)
target_link_libraries(football_ai PUBLIC football::sim_contracts football::ai_contracts)
football_configure_target(football_ai)
