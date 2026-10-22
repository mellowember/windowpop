cmake_minimum_required(VERSION 3.16)
project(WindowPop CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Host-independent core, shared by the After Effects plugin and the preview tool.
add_library(windowpop_core STATIC src/core/WindowPop.cpp)
target_include_directories(windowpop_core PUBLIC src/core)

# Renders test frames without After Effects: ./preview <outDir> [style 0-3] [look 0-1]
add_executable(preview tools/preview.cpp)
target_link_libraries(preview PRIVATE windowpop_core)
