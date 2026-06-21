cmake_minimum_required(VERSION 3.20)
project(tech-help LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(src/core)
add_subdirectory(src/core-cli)
add_subdirectory(src/ui)
add_subdirectory(src/email-watcher)
