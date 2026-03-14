# Minimal CMake project used in CI to verify that a downstream project
# can consume G4OCCT via find_package after installation.
cmake_minimum_required(VERSION 3.16 FATAL_ERROR)
project(DownstreamTest LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(G4OCCT REQUIRED)

add_executable(downstream_app downstream_app.cc)
target_link_libraries(downstream_app PRIVATE G4OCCT::G4OCCT)
