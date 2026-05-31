cmake_minimum_required(VERSION 3.15)
project(FluxGate LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(asio QUIET)

add_executable(FluxGate src/main.cpp)

if(NOT asio_FOUND)
    target_include_directories(FluxGate PRIVATE /opt/homebrew/include)
endif()

target_compile_definitions(FluxGate PRIVATE ASIO_STANDALONE)
