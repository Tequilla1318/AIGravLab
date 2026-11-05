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

# The libraries are the MISRA C++:2023 scope (docs/misra.md). They are built
# without exceptions or RTTI, so the compiler enforces that they neither throw
# nor catch, and with extra warnings that map onto MISRA rules.
function(gravlab_misra_library target)
  target_include_directories(${target} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
  gravlab_warnings(${target})
  if(NOT MSVC)
    target_compile_options(${target} PRIVATE
      -fno-exceptions -fno-rtti
      -Wsign-conversion -Wold-style-cast -Wuseless-cast -Wcast-qual
      -Wswitch-enum -Wswitch-default -Wundef -Wnull-dereference
      -Wdouble-promotion -Wimplicit-fallthrough)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
      # -Wuseless-cast is GCC only.
      target_compile_options(${target} PRIVATE -Wno-unknown-warning-option)
    endif()
  endif()
endfunction()

add_library(gravlab_egm2008 src/egm2008.cpp)
add_library(gravlab::egm2008 ALIAS gravlab_egm2008)
gravlab_misra_library(gravlab_egm2008)

add_library(gravlab_legendre_hf src/legendre_holmes_and_featherstone.cpp)
add_library(gravlab::legendre_hf ALIAS gravlab_legendre_hf)
gravlab_misra_library(gravlab_legendre_hf)

if(GRAVLAB_BUILD_TOOLS)
  add_executable(egm2008_dump tools/egm2008_dump.cpp)
  target_link_libraries(egm2008_dump PRIVATE gravlab::egm2008)
  gravlab_warnings(egm2008_dump)

  add_executable(legendre_hf_dump tools/legendre_hf_dump.cpp)
  target_link_libraries(legendre_hf_dump PRIVATE gravlab::legendre_hf)
  gravlab_warnings(legendre_hf_dump)
endif()

if(GRAVLAB_BUILD_TESTS)
  # GoogleTest, in order of preference:
  #  1. GRAVLAB_GTEST_SOURCE_DIR: build it from source with this project's
  #     flags. Needed for libc++ builds, because a packaged GoogleTest is
  #     built against libstdc++. Ubuntu's `googletest` package puts the
  #     source in /usr/src/googletest.
  #  2. An installed GoogleTest (e.g. Ubuntu's libgtest-dev).
  #  3. A download from GitHub.
  set(GRAVLAB_GTEST_SOURCE_DIR "" CACHE PATH "Build GoogleTest from this source tree")
  if(GRAVLAB_GTEST_SOURCE_DIR)
    message(STATUS "Building GoogleTest from ${GRAVLAB_GTEST_SOURCE_DIR}")
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    add_subdirectory(${GRAVLAB_GTEST_SOURCE_DIR} ${CMAKE_BINARY_DIR}/googletest EXCLUDE_FROM_ALL)
    if(NOT TARGET GTest::gtest_main)
      add_library(GTest::gtest_main ALIAS gtest_main)
    endif()
    set(GTest_FOUND TRUE)
  else()
    find_package(GTest 1.10 CONFIG QUIET)
  endif()
  if(GRAVLAB_GTEST_SOURCE_DIR)
    # already set up above
  elseif(GTest_FOUND)
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

  # Counts heap allocations in tests (replaces global operator new/delete).
  add_library(gravlab_test_support STATIC tests/support/allocation_counter.cpp)
  target_include_directories(gravlab_test_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/tests)

  add_executable(egm2008_tests tests/test_egm2008.cpp)
  target_link_libraries(egm2008_tests PRIVATE gravlab::egm2008 gravlab_test_support GTest::gtest_main)
  gravlab_warnings(egm2008_tests)

  add_executable(legendre_hf_tests tests/test_legendre_holmes_and_featherstone.cpp)
  target_link_libraries(legendre_hf_tests PRIVATE gravlab::legendre_hf gravlab_test_support GTest::gtest_main)
  gravlab_warnings(legendre_hf_tests)

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
  gtest_discover_tests(legendre_hf_tests)
endif()
