cmake_minimum_required(VERSION 3.7)
set(CMAKE_CXX_STANDARD 17)

project(clime_example DESCRIPTION "example that demonstrates use of clime.hpp")

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++1z -fexceptions -Wall -pedantic -fPIC -pthread")
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Zc:__cplusplus") # https://devblogs.microsoft.com/cppblog/msvc-now-correctly-reports-__cplusplus/
endif()

add_executable(clime_example clime_example.cpp)
include_directories(${PROJECT_SOURCE_DIR}/..)

message(STATUS "CMAKE_SYSTEM_NAME:     '${CMAKE_SYSTEM_NAME}'")
message(STATUS "CMAKE_CXX_COMPILER_ID: '${CMAKE_CXX_COMPILER_ID}'")
message(STATUS "CMAKE_BUILD_TYPE:      '${CMAKE_BUILD_TYPE}'")
message(STATUS "CMAKE_CXX_FLAGS:       '${CMAKE_CXX_FLAGS}'")
