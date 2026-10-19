# The model layer proves the protocol mapping and the scripted provider in
# isolation. It links football::model and its mock only; no AI, JSON or football
# dependency crosses this boundary.
add_executable(football_model_unit_tests model_unit_test.cpp)
# The mock provider header lives under src/model/, like every other internal
# header, so the test carries the src/ implementation root.
target_include_directories(football_model_unit_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(football_model_unit_tests
  PRIVATE
    football::model
    football::model_mock
    Catch2::Catch2WithMain)
football_configure_target(football_model_unit_tests)

add_test(NAME model_unit COMMAND football_model_unit_tests)
set_tests_properties(model_unit PROPERTIES
  LABELS "unit;model"
  TIMEOUT 30)
