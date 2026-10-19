# football::model is the model-protocol and provider-interface layer: the
# conversation protocol, the provider seam and the request/response envelopes.
# It depends on the STL only. HTTP, local inference and scripted providers are
# separate implementation targets, so a module that never calls a model never
# inherits libcurl, TLS or an inference runtime.
add_library(football_model INTERFACE)
add_library(football::model ALIAS football_model)
target_sources(football_model INTERFACE FILE_SET HEADERS
  BASE_DIRS ${PROJECT_SOURCE_DIR}/include
  FILES
    ${PROJECT_SOURCE_DIR}/include/football/model/model_protocol.hpp
    ${PROJECT_SOURCE_DIR}/include/football/model/model_provider.hpp)
target_compile_features(football_model INTERFACE cxx_std_23)

# The scripted provider is a test double and host harness, not a transport. It
# is its own target so production code carries no mock object.
add_library(football_model_mock STATIC
  mock/mock_model_provider.cpp mock/mock_model_provider.hpp)
add_library(football::model_mock ALIAS football_model_mock)
target_link_libraries(football_model_mock PUBLIC football::model)
football_configure_target(football_model_mock)
