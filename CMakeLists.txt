cmake_minimum_required(VERSION 3.14)
project(tinyengine
    DESCRIPTION "A tiled game engine for pntr_app, for web, native and ESP32"
    HOMEPAGE_URL "https://github.com/konsumer/tinyengine"
    VERSION 0.1.0
    LANGUAGES C
)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(FetchContent)

if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
  set(CMAKE_POLICY_DEFAULT_CMP0135 NEW)
endif()

if(POLICY CMP0077)
  cmake_policy(SET CMP0077 NEW)
  set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
endif()

if(POLICY CMP0169)
  cmake_policy(SET CMP0169 NEW)
  set(CMAKE_POLICY_DEFAULT_CMP0169 NEW)
endif()

# enable colors for ninja
set(CMAKE_COLOR_DIAGNOSTICS ON)

# The engine itself is a single header, so there is nothing to compile here.
add_library(tinyengine INTERFACE)
target_include_directories(tinyengine INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

if("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}")
    set(TINYENGINE_IS_MAIN TRUE)
else()
    set(TINYENGINE_IS_MAIN FALSE)
endif()

option(TINYENGINE_BUILD_EXAMPLE "tinyengine: build the demo" ${TINYENGINE_IS_MAIN})
option(TINYENGINE_BUILD_EXAMPLE_RAYLIB "tinyengine: desktop demo, through raylib" ${TINYENGINE_IS_MAIN})
option(TINYENGINE_BUILD_PREVIEW "tinyengine: headless PNG renderer, used by the tests" ${TINYENGINE_IS_MAIN})

# Baking is deliberately not a build step. tools/tiny_bake.py is an authoring tool you run
# when you change a map, like exporting from Tiled itself, and the headers it writes are
# committed. Keeping it out means the build needs nothing but a C compiler.

find_package(pntr REQUIRED)
find_package(pntr_app REQUIRED)

if(TINYENGINE_BUILD_EXAMPLE)
    add_subdirectory(examples/demo)
endif()

if(TINYENGINE_IS_MAIN)
    include(CTest)
    enable_testing()
    if(BUILD_TESTING)
        list(APPEND CMAKE_CTEST_ARGUMENTS "--output-on-failure")
        add_subdirectory(test)
    endif()
endif()
