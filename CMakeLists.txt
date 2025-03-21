cmake_minimum_required(VERSION 3.28)
project(minivim C)

set(CMAKE_C_STANDARD 23)

add_definitions(-DMINIVIM_LINUX -DMINIVIM_FILESYSTEM)

add_executable(minivim main.c)
