cmake_minimum_required(VERSION 3.10)

project(dbg_alloc C)

set(CMAKE_C_STANDARD 90)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(example1 examples/ex1.c)
