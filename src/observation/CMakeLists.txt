# Observation module implementation. Declares only its sources on the shared
# football_sim_domain target; the public world-state/snapshot values live in
# include/football/observation/, the rest stays internal.
target_sources(football_sim_domain PRIVATE
  mentalimage.cpp mentalimage.hpp
  mentalimage_sampling.cpp mentalimage_sampling.hpp
  player_observation_adapter.cpp player_observation_adapter.hpp
  player_image.hpp
  player_slots.hpp
  snapshot_history.cpp snapshot_history.hpp
  temporal_graph.cpp temporal_graph.hpp
  touch_inference.cpp touch_inference.hpp
  world_state_builder.cpp world_state_builder.hpp)
