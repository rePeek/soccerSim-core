# Contact: pure pair-contact kernel, touch proposal, impulse arbitration,
# touch difficulty and geometry. No Player, Command or Execution dependency.
add_library(football_player_contact STATIC
  player_active_touch_model.cpp
  player_active_impulse.cpp
  player_contact_kernel.cpp
  player_touch_math_kernel.cpp)
add_library(football::player_contact ALIAS football_player_contact)
target_include_directories(football_player_contact
  PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_sources(football_player_contact PUBLIC FILE_SET HEADERS
  BASE_DIRS ${PROJECT_SOURCE_DIR}/include
  FILES
    ${PROJECT_SOURCE_DIR}/include/football/player/contact/foul_assessment.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/contact/player_touch_type.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/contact/player_active_touch_model.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/contact/player_active_impulse.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/contact/player_body_collider.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/contact/player_body_collider_motion.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/contact/player_ground_collider.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/contact/player_contact_kernel.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/contact/player_touch_math_kernel.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/foul_assessment.hpp
    ${PROJECT_SOURCE_DIR}/include/football/player/player_touch_type.hpp)
target_link_libraries(football_player_contact
  PUBLIC
    football::player_state
    football::ball
    football::geometry)
target_compile_features(football_player_contact PUBLIC cxx_std_23)
football_configure_target(football_player_contact)
football_player_check_boundary(
  ${CMAKE_CURRENT_SOURCE_DIR}
  "${_player_upper_layer_regex}|#include \"player/(command|execution)/")
