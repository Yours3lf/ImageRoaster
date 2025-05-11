cmake_minimum_required(VERSION 3.5 FATAL_ERROR)

project(ImageRoaster LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_definitions(-DUNICODE)
add_definitions(-D_UNICODE)

add_executable(ImageRoaster test.cpp)
