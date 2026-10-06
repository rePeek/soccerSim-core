# Diagnostic executables own their sources here, not in the composition root.
add_executable(football_smoke football_smoke.cpp)
target_link_libraries(football_smoke PRIVATE football::game football::app_support)
football_configure_target(football_smoke)

add_executable(football_game_env_test game_env_test.cpp)
target_link_libraries(football_game_env_test PRIVATE football::game football::app_support)
football_configure_target(football_game_env_test)
add_test(NAME football_game_env_test COMMAND football_game_env_test)

add_executable(football_player_identity_test player_identity_test.cpp)
target_link_libraries(football_player_identity_test PRIVATE football::game football::app_support)
football_configure_target(football_player_identity_test)
add_test(NAME football_player_identity_test COMMAND football_player_identity_test)

add_executable(football_sim_contracts_test sim_contracts_test.cpp)
target_link_libraries(football_sim_contracts_test PRIVATE football::sim_contracts)
football_configure_target(football_sim_contracts_test)
add_test(NAME football_sim_contracts_test COMMAND football_sim_contracts_test)

add_executable(football_model_test model_test.cpp)
target_link_libraries(football_model_test PRIVATE football::model)
add_test(NAME football_model_test COMMAND football_model_test)
set_target_properties(football_model_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

add_executable(football_regression football_regression.cpp)
target_link_libraries(football_regression PRIVATE
  football::game football::app_support football_legacy_anim)
football_configure_target(football_regression)
add_test(NAME football_regression COMMAND football_regression)
add_test(NAME football_animation_ab COMMAND football_regression --animation-ab)
add_test(NAME football_animation_ab_lifecycle COMMAND football_regression --animation-ab-lifecycle)
set_tests_properties(football_regression football_animation_ab football_animation_ab_lifecycle
  PROPERTIES ENVIRONMENT "GFOOTBALL_DATA_DIR=${PROJECT_SOURCE_DIR}/data")

foreach(guard IN ITEMS model_boundary_guard module_dependency_guard runtime_animation_boundary_guard
    foundation_boundary_guard legacy_validation_guard)
  if(guard STREQUAL "module_dependency_guard")
    add_test(NAME ${guard} COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/${guard}.sh
      ${PROJECT_SOURCE_DIR}/src ${PROJECT_BINARY_DIR}/module_dependencies.txt)
  elseif(guard STREQUAL "legacy_validation_guard")
    add_test(NAME ${guard} COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/${guard}.sh ${PROJECT_SOURCE_DIR})
  else()
    add_test(NAME ${guard} COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/${guard}.sh ${PROJECT_SOURCE_DIR}/src)
  endif()
endforeach()
add_test(NAME football_headless_core_guard
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/football_headless_core_guard.sh
    $<TARGET_FILE:game> ${PROJECT_SOURCE_DIR}/src)
add_test(NAME module_source_ownership_guard
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/module_source_ownership_guard.sh
    ${PROJECT_SOURCE_DIR}/src ${PROJECT_BINARY_DIR}/module_dependencies.txt)
