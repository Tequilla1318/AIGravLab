cmake_minimum_required(VERSION 3.20)
project(GravLab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(GRAVLAB_BUILD_TESTS "Build unit tests" ON)
option(GRAVLAB_BUILD_TOOLS "Build command-line tools" ON)
option(GRAVLAB_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

function(gravlab_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4)
    if(GRAVLAB_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    if(GRAVLAB_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()

add_library(gravlab_egm2008 src/egm2008.cpp)
add_library(gravlab::egm2008 ALIAS gravlab_egm2008)
target_include_directories(gravlab_egm2008 PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
gravlab_warnings(gravlab_egm2008)

if(GRAVLAB_BUILD_TOOLS)
  add_executable(egm2008_dump tools/egm2008_dump.cpp)
  target_link_libraries(egm2008_dump PRIVATE gravlab::egm2008)
  gravlab_warnings(egm2008_dump)
endif()

if(GRAVLAB_BUILD_TESTS)
  # Prefer an installed GoogleTest (e.g. Ubuntu's libgtest-dev); download it
  # only when none is found.
  find_package(GTest 1.10 CONFIG QUIET)
  if(GTest_FOUND)
    message(STATUS "Using installed GoogleTest ${GTest_VERSION}")
  else()
    message(STATUS "GoogleTest not found; fetching v1.14.0")
    include(FetchContent)
    FetchContent_Declare(googletest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG v1.14.0
      GIT_SHALLOW TRUE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
  endif()

  enable_testing()
  add_executable(egm2008_tests tests/test_egm2008.cpp)
  target_link_libraries(egm2008_tests PRIVATE gravlab::egm2008 GTest::gtest_main)
  gravlab_warnings(egm2008_tests)

  # Full-file validation runs when the real file is available (after
  # `git lfs pull`), or when EGM2008_FILE points at a copy. Otherwise the
  # test skips itself rather than failing.
  set(GRAVLAB_EGM2008_FILE
    "${CMAKE_CURRENT_SOURCE_DIR}/externals/egm2008/EGM2008_to2190_TideFree.txt"
    CACHE FILEPATH "EGM2008 coefficient file used by the full-file test")
  target_compile_definitions(egm2008_tests PRIVATE
    GRAVLAB_EGM2008_FILE="${GRAVLAB_EGM2008_FILE}")

  include(GoogleTest)
  gtest_discover_tests(egm2008_tests)
endif()
