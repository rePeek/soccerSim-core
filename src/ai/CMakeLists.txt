# Value policy: no actor/runtime link or input ownership.
add_library(football_ai STATIC)
add_library(football::ai ALIAS football_ai)
target_sources(football_ai
  PRIVATE default_ai.cpp tactical_board.cpp
  PUBLIC FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/..
    FILES default_ai.hpp ai_config.hpp tactical_board.hpp team_requests.hpp)
target_link_libraries(football_ai PUBLIC football::sim_contracts)
football_configure_target(football_ai)
