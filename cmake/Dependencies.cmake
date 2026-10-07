# Third-party dependencies, each pinned to an exact release.
# Add a dependency here only in the stage that first needs it:
#   zlib          stage 2 (.xmpuzzle files are gzip-compressed XML)
#   nlohmann/json stage 7 (engine JSON-RPC)
include(FetchContent)

if(BT_BUILD_TESTS)
  # The same Catch2 release as the original BurrTools test suite.
  FetchContent_Declare(Catch2
    URL https://github.com/catchorg/Catch2/archive/v3.16.0.tar.gz
    URL_HASH SHA256=0957cae5821b17ce07f0833aaa52b5137643a8382203221f363a8303c109af34
    FIND_PACKAGE_ARGS 3.16)
  FetchContent_MakeAvailable(Catch2)
endif()
