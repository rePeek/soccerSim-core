# Observation capability module: capture history, mental images, temporal graph
# and touch inference. It never owns live match state and never links Runtime.
# The WorldState builder is a Runtime-side adapter (src/runtime) because its
# input is a borrowed live actor view; the public value types stay in contracts.
add_library(football_observation STATIC
  mentalimage.cpp mentalimage.hpp
  mentalimage_sampling.cpp mentalimage_sampling.hpp
  player_observation_adapter.cpp player_observation_adapter.hpp
  player_image.hpp
  player_slots.hpp
  snapshot_history.cpp snapshot_history.hpp
  temporal_graph.cpp temporal_graph.hpp
  touch_inference.cpp touch_inference.hpp)
add_library(football::observation ALIAS football_observation)
target_include_directories(football_observation PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(football_observation
  PUBLIC football::sim_contracts
  PRIVATE
    football::ball
    football::player_execution
    football::animation
    football::foundation)
football_configure_target(football_observation)
