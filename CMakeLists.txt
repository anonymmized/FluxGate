cmake_minimum_required(VERSION 3.15)
project(FluxGate LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(asio QUIET)

add_library(FluxGateCore
    src/admin_server.cpp
    src/cache.cpp
    src/cert_authority.cpp
    src/config.cpp
    src/connect_parser.cpp
    src/filter.cpp
    src/http_message.cpp
    src/leaf_certificate_cache.cpp
    src/metrics.cpp
    src/mitm_tls.cpp
    src/proxy_server.cpp
)

target_include_directories(FluxGateCore PUBLIC include)

if(asio_FOUND)
    target_link_libraries(FluxGateCore PUBLIC asio::asio)
else()
    target_include_directories(FluxGateCore PUBLIC /opt/homebrew/include)
endif()

target_link_libraries(FluxGateCore PUBLIC OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
target_compile_definitions(FluxGateCore PUBLIC ASIO_STANDALONE)

add_executable(FluxGate src/main.cpp)
target_link_libraries(FluxGate PRIVATE FluxGateCore)

enable_testing()

add_executable(FluxGateTests tests/unit_tests.cpp)
target_link_libraries(FluxGateTests PRIVATE FluxGateCore)
add_test(NAME FluxGateTests COMMAND FluxGateTests)
