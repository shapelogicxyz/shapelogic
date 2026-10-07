# Third-party dependencies, each pinned to an exact release.
# Add a dependency here only in the stage that first needs it:
#   zlib          stage 2 (.xmpuzzle files are gzip-compressed XML)
#   nlohmann/json stage 7 (engine JSON-RPC)
include(FetchContent)

# zlib: the system's when there is one (macOS, Linux), else built from the
# pinned release (MinGW). Only the static library is used.
FetchContent_Declare(ZLIB
  URL https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz
  URL_HASH SHA256=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
  EXCLUDE_FROM_ALL
  FIND_PACKAGE_ARGS)
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(ZLIB)
if(NOT TARGET ZLIB::ZLIB)
  # built from source: zlib's own CMake has no namespaced target
  target_include_directories(zlibstatic INTERFACE ${zlib_SOURCE_DIR} ${zlib_BINARY_DIR})
  add_library(ZLIB::ZLIB ALIAS zlibstatic)
endif()

if(BT_BUILD_TESTS)
  # The same Catch2 release as the original BurrTools test suite.
  FetchContent_Declare(Catch2
    URL https://github.com/catchorg/Catch2/archive/v3.16.0.tar.gz
    URL_HASH SHA256=0957cae5821b17ce07f0833aaa52b5137643a8382203221f363a8303c109af34
    FIND_PACKAGE_ARGS 3.16)
  FetchContent_MakeAvailable(Catch2)
endif()
