# BqLog's upstream CMake entry point is src/, not its repository root.
if(WIN32)
  set(_football_bq_platform win64)
elseif(ANDROID)
  set(_football_bq_platform android)
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  set(_football_bq_platform ios)
elseif(APPLE)
  set(_football_bq_platform mac)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(_football_bq_platform linux)
elseif(UNIX)
  set(_football_bq_platform unix)
else()
  message(FATAL_ERROR "Unsupported BqLog platform: ${CMAKE_SYSTEM_NAME}")
endif()

CPMAddPackage(NAME BqLog
  GITHUB_REPOSITORY Tencent/BqLog
  # Release_2.5.0, pinned to the release commit for reproducible builds.
  GIT_TAG 89af0fe488d3ba1e3760fb889751311032da0a8d
  SOURCE_SUBDIR src
  OPTIONS
    "TARGET_PLATFORM ${_football_bq_platform}"
    "BUILD_LIB_TYPE static_lib"
    "APPLE_LIB_FORMAT a"
    "JAVA_SUPPORT OFF" "NODE_API_SUPPORT OFF" "PYTHON_SUPPORT OFF" "GO_SUPPORT OFF")

# Upstream uses directory-local include paths and writes archives into its checkout.
# Export only public headers; isolate archives per build directory. Upstream's
# post-build header copy remains in its CPM checkout.
target_include_directories(BqLog SYSTEM INTERFACE "${BqLog_SOURCE_DIR}/include")
set_target_properties(BqLog PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/_deps/bqlog-lib")
# Disable upstream's startup/debug chatter even in our Debug preset. This is
# private to BqLog: simulator assertions remain enabled in Debug.
target_compile_definitions(BqLog PRIVATE NDEBUG)
# Nix's fortify preprocessor warning at -O0 must not become upstream's -Werror.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(BqLog PRIVATE -Wno-error=cpp)
endif()
