cmake_minimum_required(VERSION 3.19.0)
project(timer_pool)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wconversion -pedantic")
elseif(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
endif()

add_subdirectory(libs)

add_library(timer_pool INTERFACE include/timer_pool/timer_pool.hpp)
target_include_directories(timer_pool INTERFACE include/)
target_link_libraries(timer_pool INTERFACE timer_pool_external_libs)

add_executable(timer_pool_example examples/example1.cpp)
target_link_libraries(timer_pool_example PRIVATE timer_pool)