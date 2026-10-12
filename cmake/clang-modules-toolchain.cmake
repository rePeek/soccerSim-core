# Toolchain for the C++20 named-module build (Clang 19 under nix, libc++).
#
# Two nix quirks are handled here:
#  1. clang-scan-deps does not apply the nix clang wrapper's implicit include
#     paths, so the compiler's system include directories are captured once here
#     and applied to both compilation and module dependency scanning.
#  2. llvmPackages.libcxxStdenv installs libc++ headers under
#     <libcxx>/include/c++/v1 but the wrapper only adds <libcxx>/include, so
#     Clang silently falls back to the (newer) libstdc++. That mismatch breaks
#     Catch2 decomposition; point the compiler at the real libc++ root and
#     forbid the libstdc++ fallback.
#
# Usage: applied automatically by the debug/release CMake presets. For a
# bare configure, pass -DCMAKE_TOOLCHAIN_FILE=cmake/clang-modules-toolchain.cmake
# (and use Ninja: the module scanner needs a dyndep-capable generator).

set(CMAKE_CXX_COMPILER "clang++" CACHE FILEPATH "C++ compiler")

# Scan every C++ source for imports so targets that import football.pitch (but
# do not themselves define modules) get the module BMI on their compile line.
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR NOT CMAKE_CXX_COMPILER_ID)
  # libc++ include root (nix libcxxStdenv puts headers under c++/v1).
  set(_football_libcxx "")
  if(DEFINED ENV{CMAKE_INCLUDE_PATH})
    string(REPLACE ":" ";" _football_inc_path "$ENV{CMAKE_INCLUDE_PATH}")
    foreach(_p IN LISTS _football_inc_path)
      if(_p MATCHES "libcxx.*/include$")
        set(_football_libcxx "${_p}/c++/v1")
      endif()
    endforeach()
  endif()
  if(_football_libcxx)
    string(APPEND CMAKE_CXX_FLAGS " -stdlib=libc++ -nostdinc++ -isystem ${_football_libcxx}")
  endif()

  execute_process(
    COMMAND "${CMAKE_CXX_COMPILER}" -stdlib=libc++ -nostdinc++ -isystem "${_football_libcxx}" -v -E -x c++ /dev/null
    OUTPUT_QUIET ERROR_VARIABLE _football_cxx_verbose)
  string(REPLACE "\n" ";" _football_cxx_lines "${_football_cxx_verbose}")
  set(_football_in_list FALSE)
  set(_football_include_dirs "")
  foreach(_line IN LISTS _football_cxx_lines)
    if(_line MATCHES "search starts here")
      set(_football_in_list TRUE)
    elseif(_line MATCHES "End of search list")
      set(_football_in_list FALSE)
    elseif(_football_in_list AND _line MATCHES "^ +(/nix/store[^ ]+)")
      list(APPEND _football_include_dirs "${CMAKE_MATCH_1}")
    endif()
  endforeach()
  foreach(_dir IN LISTS _football_include_dirs)
    string(APPEND CMAKE_CXX_FLAGS " -isystem ${_dir}")
  endforeach()
endif()
