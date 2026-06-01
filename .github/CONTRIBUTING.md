# Contributing to FluxGate

## Prerequisites

- C++20 compiler (clang ≥ 14 or gcc ≥ 12)
- CMake ≥ 3.15
- OpenSSL ≥ 1.1

## Build + test

```sh
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

To enable Redis support:

```sh
cmake -S . -B build -DFLUXGATE_REDIS=ON
```

## Project layout

```
include/fluxgate/   public headers
src/                implementation
tests/              unit_tests.cpp + integration_tests.cpp
docs/               architecture and ops notes
```

## Adding a filter

1. Add a class in `include/fluxgate/filter.h` that inherits `TrafficFilter`.
2. Implement `name()` and `apply()` in `src/filter.cpp`.
3. Add unit tests in `tests/unit_tests.cpp`.
4. Wire into `ProxyServer` constructor in `src/proxy_server.cpp` if it needs a config knob.

## Code style

- C++20, no exceptions in hot paths, no `new`/`delete` directly.
- Async operations via Asio strands only — never block `io_context` threads.
- No body content in logs — keep `logger.h` event fields to metadata only.

## Pull requests

- One logical change per PR.
- All tests must pass: `ctest --test-dir build --output-on-failure`.
- Update `README.md` if you add a CLI flag or config key.
