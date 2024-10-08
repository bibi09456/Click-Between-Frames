cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_ARCHITECTURES "arm64")
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
# set(SKIP_BUILDING_CODEGEN OFF)

project(ClickBetweenFrames VERSION 1.0.0)

add_library(${PROJECT_NAME} SHARED
    src/main.cpp
)

if (WIN32)
    target_sources(${PROJECT_NAME} PRIVATE src/windows.cpp)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Android")
    target_sources(${PROJECT_NAME} PRIVATE src/android.cpp)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
    set_source_files_properties(src/macos.mm PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
    target_sources(${PROJECT_NAME} PRIVATE src/macos.mm)
endif()

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

setup_geode_mod(${PROJECT_NAME})
