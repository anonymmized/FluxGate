cmake_minimum_required(VERSION 3.15)
project(FluxGate LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(asio QUIET)

add_executable(FluxGate src/main.cpp)

if(asio_FOUND)
    target_link_libraries(FluxGate PRIVATE asio::asio)
else()
    target_include_directories(FluxGate PRIVATE /opt/homebrew/include)
endif()

target_link_libraries(FluxGate PRIVATE OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
target_compile_definitions(FluxGate PRIVATE ASIO_STANDALONE)
