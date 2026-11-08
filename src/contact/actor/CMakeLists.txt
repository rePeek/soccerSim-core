# Contact actor adapter: reads concrete Player state, calls the pure Contact
# kernel and applies body reactions. This is the only Contact target that may
# depend on Player Execution; football::contact stays a pure value kernel.
add_library(football_contact_actor STATIC
  player_contact_execution.cpp player_contact_execution.hpp)
add_library(football::contact_actor ALIAS football_contact_actor)
target_include_directories(football_contact_actor PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(football_contact_actor
  PUBLIC football::sim_contracts
  PRIVATE
    football::contact football::player football::possession football::team
    football::ball football::foundation)
football_configure_target(football_contact_actor)
