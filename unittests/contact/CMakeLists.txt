# Contact must link without concrete Player, Animation runtime or Simulation.
add_executable(football_contact_unit_tests contact_test.cpp)
target_link_libraries(football_contact_unit_tests PRIVATE
  football::contact Catch2::Catch2WithMain)
football_configure_target(football_contact_unit_tests)
add_test(NAME contact_unit COMMAND football_contact_unit_tests)
set_tests_properties(contact_unit PROPERTIES LABELS "unit;contact" TIMEOUT 30)

add_executable(football_contact_link_smoke contact_link_smoke.cpp)
target_link_libraries(football_contact_link_smoke PRIVATE
  "$<LINK_LIBRARY:WHOLE_ARCHIVE,football_contact>")
football_configure_target(football_contact_link_smoke)
add_test(NAME contact_link_smoke COMMAND football_contact_link_smoke)
set_tests_properties(contact_link_smoke PROPERTIES LABELS "interface;contact" TIMEOUT 30)

# Compile each canonical header separately using exported include roots only.
file(GLOB _contact_headers ${PROJECT_SOURCE_DIR}/include/football/contact/*.hpp)
set(_contact_smoke_sources)
foreach(_header ${_contact_headers})
  get_filename_component(_name ${_header} NAME)
  set(_source ${CMAKE_CURRENT_BINARY_DIR}/${_name}.cpp)
  file(GENERATE OUTPUT ${_source} CONTENT "#include \"football/contact/${_name}\"\n")
  list(APPEND _contact_smoke_sources ${_source})
endforeach()
add_library(football_contact_header_smoke OBJECT ${_contact_smoke_sources})
target_link_libraries(football_contact_header_smoke PRIVATE football::contact)
football_configure_target(football_contact_header_smoke)
