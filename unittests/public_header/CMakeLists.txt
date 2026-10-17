# S20 public-contract smoke. The object library only sees the exported include
# roots (football::sim_contracts / ai_contracts / ball); it must never add the
# src/ implementation root. A successful configure+build is the acceptance that
# include/football/env.hpp and its transitive public headers are self-contained.
add_library(football_public_header_smoke OBJECT public_header_smoke.cpp)
target_link_libraries(football_public_header_smoke
  PRIVATE
    football::sim_contracts
    football::ai_contracts
    football::ball)
football_configure_target(football_public_header_smoke)
