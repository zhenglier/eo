cmake_minimum_required(VERSION 3.14)
project(execution_order)

set(CMAKE_CXX_STANDARD 14)

add_subdirectory(solution)

add_executable(execution_order main.cpp)

target_link_libraries(execution_order solution_lib)
