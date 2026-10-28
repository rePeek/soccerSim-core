# Football-domain contact models. No concrete Player, execution or Runtime.
add_library(football_contact STATIC
  player_contact_kernel.cpp
  player_active_touch_model.cpp
  player_active_impulse.cpp
  passive_body_touch.cpp
  contact_evidence.cpp)
add_library(football::contact ALIAS football_contact)
target_sources(football_contact PUBLIC FILE_SET HEADERS
  BASE_DIRS ${PROJECT_SOURCE_DIR}/include
  FILES
    ${PROJECT_SOURCE_DIR}/include/football/contact/player_contact_kernel.hpp
    ${PROJECT_SOURCE_DIR}/include/football/contact/player_active_touch_model.hpp
    ${PROJECT_SOURCE_DIR}/include/football/contact/player_active_impulse.hpp
    ${PROJECT_SOURCE_DIR}/include/football/contact/player_action_volume.hpp
    ${PROJECT_SOURCE_DIR}/include/football/contact/player_body_collider.hpp
    ${PROJECT_SOURCE_DIR}/include/football/contact/player_body_collider_motion.hpp
    ${PROJECT_SOURCE_DIR}/include/football/contact/player_ground_collider.hpp
    ${PROJECT_SOURCE_DIR}/include/football/contact/player_touch_type.hpp
    ${PROJECT_SOURCE_DIR}/include/football/contact/foul_assessment.hpp
    ${PROJECT_SOURCE_DIR}/include/football/contact/passive_body_touch.hpp
    ${PROJECT_SOURCE_DIR}/include/football/contact/contact_evidence.hpp
    # Flat compatibility paths; canonical headers live under football/contact/.
    ${PROJECT_SOURCE_DIR}/include/football/player/foul_assessment.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/player_touch_type.hpp)
target_link_libraries(football_contact PUBLIC
  football::player_state football::ball football::geometry football::collision)
football_configure_target(football_contact)

# Scan both implementations and exported headers, including public include paths.
file(GLOB_RECURSE _contact_files
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
  ${PROJECT_SOURCE_DIR}/include/football/contact/*.hpp)
# Player's flat headers have separate owners: Contact may consume only the
# model FILE_SET, not a same-directory input or execution forwarding header.
get_target_property(_contact_model_headers football_player_state HEADER_SET)
football_player_check_contract_files(
  "#include \"(football/)?(runtime|referee|ai|event|observation|team)/|#include \"(football/)?player/(execution|command|contact)/|#include \"player/player.hpp\""
  "${_contact_model_headers}" ${_contact_files})
