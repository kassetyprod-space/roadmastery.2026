cmake_minimum_required(VERSION 3.16)
project(PeregonVehicleSim CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Ядро лежит внутри UE-модуля VehicleSimCore, но не зависит от Unreal.
# Берём только vsim/*.cpp — файл модуля (VehicleSimCoreModule.cpp) требует движка.
set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../Source/VehicleSimCore)
file(GLOB CORE_SOURCES ${CORE_DIR}/Private/vsim/*.cpp)

add_library(vsim STATIC ${CORE_SOURCES})
target_include_directories(vsim PUBLIC ${CORE_DIR}/Public)
if(MSVC)
  target_compile_options(vsim PRIVATE /W4 /utf-8)
else()
  target_compile_options(vsim PRIVATE -Wall -Wextra -Wshadow -Wconversion -Werror)
endif()

add_executable(vsim_tests VehicleSimTests.cpp)
target_link_libraries(vsim_tests PRIVATE vsim)
if(MSVC)
  target_compile_options(vsim_tests PRIVATE /utf-8)
endif()

enable_testing()
add_test(NAME vsim_tests COMMAND vsim_tests)

add_executable(vmodel_tests BodyModelTests.cpp)
target_link_libraries(vmodel_tests PRIVATE vsim)
if(MSVC)
  target_compile_options(vmodel_tests PRIVATE /utf-8)
endif()
add_test(NAME vmodel_tests COMMAND vmodel_tests)
