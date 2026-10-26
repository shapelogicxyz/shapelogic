# Decides SL_VERSION, the project's one version, before project() (see the
# top-level CMakeLists): SHAPELOGIC_VERSION in the environment (the Release
# workflow sets it from the tag it builds), else the newest vX.Y[.Z] tag
# `git describe` finds, else 0.0.0 (a source tree without git). vX.Y is
# X.Y.0. web/tools/project-version.ts follows the same rule; the app checks
# the engine says the same version as itself.
function(_sl_version_from_tag tag out)
  if(tag MATCHES "^v?([0-9]+)\\.([0-9]+)(\\.([0-9]+))?$")
    if("${CMAKE_MATCH_4}" STREQUAL "")
      set(${out} "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.0" PARENT_SCOPE)
    else()
      set(${out} "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_4}" PARENT_SCOPE)
    endif()
  else()
    set(${out} "" PARENT_SCOPE)
  endif()
endfunction()

set(SL_VERSION "")
if(DEFINED ENV{SHAPELOGIC_VERSION} AND NOT "$ENV{SHAPELOGIC_VERSION}" STREQUAL "")
  _sl_version_from_tag("$ENV{SHAPELOGIC_VERSION}" SL_VERSION)
  if(SL_VERSION STREQUAL "")
    message(FATAL_ERROR "SHAPELOGIC_VERSION=$ENV{SHAPELOGIC_VERSION}: not vX.Y or vX.Y.Z")
  endif()
else()
  find_package(Git QUIET)
  if(Git_FOUND)
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" describe --tags --match "v[0-9]*" --abbrev=0
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      OUTPUT_VARIABLE _sl_tag
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
      RESULT_VARIABLE _sl_git_result)
    if(_sl_git_result EQUAL 0)
      _sl_version_from_tag("${_sl_tag}" SL_VERSION)
    endif()
  endif()
  # a new tag configures again, so the engine and the app keep one version
  foreach(_sl_ref IN ITEMS .git/refs/tags .git/packed-refs)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_sl_ref}")
      set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/${_sl_ref}")
    endif()
  endforeach()
endif()
if(SL_VERSION STREQUAL "")
  set(SL_VERSION "0.0.0")
endif()
message(STATUS "ShapeLogic version ${SL_VERSION}")
