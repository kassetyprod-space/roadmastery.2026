# Все headless-тесты ядер (без Unreal): cmake -S Tests -B build && ctest --test-dir build
cmake_minimum_required(VERSION 3.16)
project(PeregonTests CXX)
enable_testing()
add_subdirectory(VehicleSim)
add_subdirectory(RoadGen)
