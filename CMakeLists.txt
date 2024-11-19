cmake_minimum_required(VERSION 3.10)
project(DroneComm)

set(CMAKE_CXX_STANDARD 17)

# Include directories
include_directories(include)

# Source files
set(SOURCES
        src/drone.cpp
        src/network_utils.cpp
)

# Add executable target
add_executable(drone ${SOURCES})

# Link Windows networking library
target_link_libraries(drone ws2_32)
