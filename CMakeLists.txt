cmake_minimum_required(VERSION 3.15)
project(FluxGate LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

include(FetchContent)

# ── Asio (standalone, header-only) ──────────────────────────────────────────
# Prefer a system-installed asio (find_package or a local include dir); fall
# back to fetching a pinned release so the build is reproducible everywhere
# (CI runners and fresh clones don't have asio installed system-wide).
find_package(asio QUIET)
if(NOT asio_FOUND)
    FetchContent_Declare(
        asio
        GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
        GIT_TAG asio-1-30-2
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(asio)
    # The asio repo has no root CMake target — add its include dir manually.
    add_library(asio_headers INTERFACE)
    target_include_directories(asio_headers INTERFACE ${asio_SOURCE_DIR}/asio/include)
endif()

FetchContent_Declare(
    simdjson
    GIT_REPOSITORY https://github.com/simdjson/simdjson.git
    GIT_TAG v3.10.0
    GIT_SHALLOW TRUE
)
set(SIMDJSON_JUST_LIBRARY ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(simdjson)

FetchContent_Declare(
    tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG v3.4.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(tomlplusplus)

option(FLUXGATE_REDIS "Build with Redis cache backend (requires hiredis)" OFF)
if(FLUXGATE_REDIS)
    FetchContent_Declare(
        hiredis
        GIT_REPOSITORY https://github.com/redis/hiredis.git
        GIT_TAG v1.2.0
        GIT_SHALLOW TRUE
    )
    set(DISABLE_TESTS ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(hiredis)
endif()

set(FLUXGATE_SOURCES
    src/admin_server.cpp
    src/cache.cpp
    src/cert_authority.cpp
    src/config.cpp
    src/connect_parser.cpp
    src/filter.cpp
    src/http_message.cpp
    src/leaf_certificate_cache.cpp
    src/logger.cpp
    src/metrics.cpp
    src/mitm_tls.cpp
    src/pricing.cpp
    src/proxy_server.cpp
    src/rate_limiter.cpp
    src/setup_wizard.cpp
    src/tui.cpp
)
if(FLUXGATE_REDIS)
    list(APPEND FLUXGATE_SOURCES src/redis_cache.cpp)
endif()

add_library(FluxGateCore ${FLUXGATE_SOURCES})

target_include_directories(FluxGateCore PUBLIC include)

if(asio_FOUND)
    target_link_libraries(FluxGateCore PUBLIC asio::asio)
else()
    target_link_libraries(FluxGateCore PUBLIC asio_headers)
endif()

target_link_libraries(FluxGateCore PUBLIC OpenSSL::SSL OpenSSL::Crypto Threads::Threads simdjson tomlplusplus::tomlplusplus)
if(FLUXGATE_REDIS)
    target_link_libraries(FluxGateCore PUBLIC hiredis::hiredis)
    target_compile_definitions(FluxGateCore PUBLIC FLUXGATE_HAS_REDIS)
endif()
target_compile_definitions(FluxGateCore PUBLIC ASIO_STANDALONE)

add_executable(FluxGate src/main.cpp)
target_link_libraries(FluxGate PRIVATE FluxGateCore)

enable_testing()

add_executable(FluxGateTests tests/unit_tests.cpp)
target_link_libraries(FluxGateTests PRIVATE FluxGateCore)
add_test(NAME FluxGateTests COMMAND FluxGateTests)

add_executable(FluxGateIntegration tests/integration_tests.cpp)
target_link_libraries(FluxGateIntegration PRIVATE FluxGateCore)
add_test(NAME FluxGateIntegration COMMAND FluxGateIntegration)
