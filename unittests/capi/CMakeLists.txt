# Compile the consumer with a C compiler, not a C++ compiler.
add_executable(football_capi_tests capi_test.c)
target_link_libraries(football_capi_tests PRIVATE football::simcore)
set_target_properties(football_capi_tests PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF
  RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
add_test(NAME capi_unit COMMAND football_capi_tests)
set_tests_properties(capi_unit PROPERTIES LABELS "unit;capi" TIMEOUT 10)
