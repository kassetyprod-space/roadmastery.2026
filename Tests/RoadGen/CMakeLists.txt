cmake_minimum_required(VERSION 3.16)
project(PeregonRoadGen CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(RGEN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../Source/RoadGenCore)
file(GLOB RGEN_SOURCES ${RGEN_DIR}/Private/rgen/*.cpp)

add_library(rgen STATIC ${RGEN_SOURCES})
target_include_directories(rgen PUBLIC ${RGEN_DIR}/Public)
if(MSVC)
  target_compile_options(rgen PRIVATE /W4 /utf-8)
else()
  target_compile_options(rgen PRIVATE -Wall -Wextra -Wshadow -Wconversion -Werror)
endif()

add_executable(rgen_tests RoadGenTests.cpp)
target_link_libraries(rgen_tests PRIVATE rgen)
target_compile_definitions(rgen_tests PRIVATE PEREGON_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
if(MSVC)
  target_compile_options(rgen_tests PRIVATE /utf-8)
endif()

enable_testing()
add_test(NAME rgen_tests COMMAND rgen_tests)
