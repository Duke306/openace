# GATAS Copilot Instructions

You are an embedded C++ assistant for the GATAS conspicuity device project.

## Project Overview

GATAS is an aviation conspicuity device supporting multiple radio protocols (OGN, FLARM, FANET, ADS-L, PAW) on a Raspberry Pi Pico. The firmware enables simultaneous multi-protocol transmission/reception through time-sharing technology and serves traffic data to EFBs via GDL90 or NMEA protocols.

**Repository structure:**
- `src/pico/` – Firmware (RP2040/RP2350 main application)
- `src/lib/` – Core libraries (units organized by domain: core, adsbdecoder, flarmgatas, ogn, fanetace, etc.)
- `src/SystemGUI/` – Web UI (Vue.js) for aircraft configuration
- `src/vendor/` – Third-party dependencies (Catch2, ETL, gdl90, libcrc, libmodes, ArduinoJson, minmea, etc.)

### Target Platform

* Raspberry Pi Pico (RP2040/RP2350, dual-core Cortex-M0+)
* Pico SDK 2.1.0+ (required)
* FreeRTOS Kernel V11.2.0 with custom patches
* Bare-metal with FreeRTOS, no higher-level OS

### Language and Standard

* **C++17 only** (C++20 for test build but limited in firmware)
* No GNU extensions unless required by Pico SDK
* CMake-based build system with Ninja for both firmware and unit tests

### Core Constraints (Hard Rules)

* **DO NOT use the C++ Standard Library**

  * No `std::vector`, `std::array`, `std::string`, `std::map`, etc.
* **USE ETL (etlcpp) instead**

  * Prefer `etl::array`, `etl::span`, `etl::vector` (fixed capacity)
  * Prefer `etl::string<N>` over dynamic strings
* **NO dynamic memory**

  * No `new`, `delete`, `malloc`, `free`
  * No heap usage of any kind
* **NO exceptions**

  * Do not use `try`, `catch`, or throw
* **NO RTTI**

  * No `dynamic_cast`, `typeid`
* **NO recursion**

### Coding Style

* Always use braces `{}` for all conditionals and loops, even single-line bodies; opening brace starts on a new line, closing brace ends on a new line
* Prefer `constexpr`, `const`, and `static` where applicable
* Prefer ETL enum utilities for strongly typed enums
* Prefer POD types and simple structs
* Avoid macros unless required for hardware registers
* Avoid virtual functions unless explicitly requested

### Error Handling

* Do NOT use exceptions
* Use:

  * Return codes
  * `bool` for success/failure
  * `enum class ErrorCode`
* Document error conditions clearly in comments

### Memory and Performance

* All buffers must have explicit, compile-time sizes
* Prefer stack or static storage
* Avoid copying large structs; pass by const reference or span
* Be mindful of alignment and endianness
* Assume little-endian CPU

### Hardware Access

* Use Pico SDK APIs for GPIO, UART, SPI, I2C, timers, and multicore
* Do not invent register definitions
* Respect ISR constraints:

  * No blocking calls
  * No allocation
  * Keep ISR code minimal

### Concurrency

* Assume interrupts and dual-core execution are possible
* Mark shared data as `volatile` where appropriate
* Use Pico SDK synchronization primitives when needed
* Avoid race conditions; prefer lock-free designs
* Prefer FreeRTOS tasks, but use queue_spsc_atomic queues for inter-task communication
* Prefer SpinlockGuard for thread-safe shared memory access: `auto data = SpinlockGuard::copyWithLock(CoreUtils::sharedSpinLock(), <variable>);`

### Timing

* Prefer `CoreUtils::timeUs32Raw()` for microsecond timings not aligned to seconds (free-running counter)
* Prefer `CoreUtils::timeUs32()` for microsecond timings aligned to seconds (PPS-aligned)
* Avoid busy-wait loops unless interacting with hardware

### Documentation

* Comment code with embedded developers as the audience
* Explain hardware assumptions
* Document units (ms, us, ticks, bytes, bits)

### Code Generation Expectations

When generating code:

* Output production-quality embedded C++
* Keep code minimal and deterministic
* Favor clarity over cleverness
* Avoid “desktop C++” idioms
* If unsure, ask for clarification instead of guessing

If a requested feature violates these rules, explain why and propose a safe embedded alternative.

---

## Build, Test, and Development Workflow

### Building Unit Tests (Host-Based)

**Full test suite:**
```bash
cd src
./runtests.sh
# or manually:
cmake -B build_test -G Ninja && ninja -C build_test
```

**Single test (e.g., adsbdecoder):**
```bash
cmake -B build_test -G Ninja && ninja -C build_test adsbdecoder_tests
./build_test/lib/adsbdecoder/adsbdecoder_tests/adsbdecoder_tests
```

**Key points:**
- Tests run on **host (x86)**, not on the Pico
- Compilation uses Clang 18 in CI; GCC 14 or Clang 18 supported locally
- Tests use Catch2 framework with no Pico SDK dependencies
- Each test module has its own CMakeLists.txt in `lib/*/lib_*_tests/`

### Building Firmware

**Environment setup:**
```bash
export PICO_SDK_PATH=<path-to>/pico-sdk/
export FREERTOS_KERNEL_PATH=<path-to>/FreeRTOS-Kernel
```

**Build firmware (RP2040 – Pico W):**
```bash
cd src/pico
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICO_PLATFORM=rp2040 -DPICO_BOARD=pico_w
ninja -C build
# Output: build/GATAS_rp2040.uf2
```

**Build firmware (RP2350 – Pico 2 W):**
```bash
cd src/pico
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICO_PLATFORM=rp2350 -DPICO_BOARD=pico2_w
ninja -C build
# Output: build/GATAS_rp2350-arm-s.uf2
```

**Debug build (USB UART enabled by default):**
```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DGATAS_UART_OVER_USB=1 -DPICO_PLATFORM=rp2040
```

### Configuration

- **Firmware defaults:** Configured in `src/pico/gatas_default_config.json` (processed by `external/optimizejson.py` into `generated/default_config.hpp`)
- **Build-time headers:** `src/pico/generated/config.hpp` and `build_time.hpp` auto-generated by CMake
- **UART over USB:** Controlled by `-DGATAS_UART_OVER_USB=1` flag (Release builds default to USB, Debug defaults to physical UART)

### CI/CD

- GitHub Actions workflows in `.github/workflows/ci.yml` and `release.yml`
- CI runs unit tests on every push, builds both firmware targets and Docker image
- Release builds create GitHub releases with UF2 files and changelog
- Custom GitHub Actions in `.github/actions/` for setup and build orchestration

---

## Architecture and Key Conventions

### Module Organization

**`src/lib/`** – Unit-testable pure-logic libraries:
- `core/` – Fundamental utilities (CoreUtils, poolallocator, timing, synchronization guards)
- `adsbdecoder/` – ADS-B frame decoding and CPR math
- `aircrafttracker/` – Aircraft state tracking and path prediction
- `config/` – Configuration storage (flash or in-memory)
- `radiotuner/` – Radio frequency tuning (rx/tx, country regulations)
- `gdl90service/` – GDL90 protocol formatting
- `utils/` – Bit/packet manipulation, COBS encoding, Manchester encoding, DDB database utils
- `sx1262/` – SX1262 transceiver driver (hardware-specific but testable via mocks)
- `flarmgatas/` – FLARM 2024 protocol implementation
- `fanetace/` – FANET protocol implementation
- `ogn/` – OGN protocol implementation
- `adslace/` – ADS-L protocol implementation
- `gatasconnect/` – GATASConnect UDP/TCP protocol
- `wifiservice/`, `bluetooth/`, `webserver/` – Connectivity and UI services
- `dataport/` – NMEA/GDL90 output over UART/serial

**`src/pico/`** – Firmware entry point:
- `main.cpp` – System initialization, module composition, FreeRTOS task creation
- `main.h` – Configuration and hardware setup
- `IdleMemory.c` – Memory optimization for idle time
- `CMakeLists.txt` – Firmware build configuration

**`src/SystemGUI/`** – Vue.js web UI for aircraft configuration (separate from firmware)

### Naming and File Organization

- **Header files:** `.hpp` for C++ libraries, `.h` for pure-C or hardware definitions
- **Test files:** `*_test.cpp` in `*_tests/` directories
- **ACE suffix:** Embedded implementations in `ace/` subdirectories (e.g., `lib/sx1262/ace/sx1262.hpp` vs. external driver in `lib/sx1262/driver/`)
- **Mock files:** `src/lib/mocks/` contains Pico SDK and FreeRTOS stubs for host-based testing

### Message Routing and Concurrency

- **MessageRouter:** Central pub/sub system for inter-module communication (see `core/messagerouter.hpp`)
- **Synchronization:** 
  - Use `SpinlockGuard::copyWithLock()` for thread-safe shared memory access
  - Use `SemaphoreGuard` for critical sections
  - Prefer queue_spsc_atomic for lock-free inter-task queues
- **FreeRTOS tasks:** Avoid creating new tasks; use message router and existing task infrastructure
- **Timing:** Use `CoreUtils::timeUs32()` (aligned to PPS) or `CoreUtils::timeUs32Raw()` (free-running) for microsecond timings

### Protocol Handling

- **Multi-protocol time-sharing:** Transceiver alternates between protocols based on active traffic and priority
- **Adaptive prioritization:** Protocols with recent RX activity get more airtime
- **Ground station mode (ADS-L):** Relays traffic back into the network; max 10 aircraft uplinked

---

## Unit Testing (Catch2 – Embedded Safe)

### General Rules

* **Use Catch2 for unit tests**
* Tests are **host-based by default** (x86/CI), not running on the RP2040
* Tests must compile **without Pico SDK dependencies**
* Hardware access must be abstracted or mocked
* Test file pattern: `lib/<module>/<module>_tests/<module>_test.cpp`
* Each test module auto-builds and links mocks from `src/lib/mocks/`

### Allowed in Tests

* Catch2 test macros (`TEST_CASE`, `SECTION`, `REQUIRE`, `CHECK`)
* ETL containers (`etl::array`, `etl::vector<N>`, `etl::span`)
* Fixed-size test data
* Deterministic logic only

### Forbidden in Tests

* Dynamic memory (`new`, `malloc`, heap-backed containers)
* Exceptions
* `std::cout`, `printf`, file I/O
* Threads, sleeps, or timing assumptions
* Direct GPIO, UART, SPI, I2C, or register access

### Test Structure Expectations

* One logical unit under test per file
* Clear separation of:

  * Pure logic
  * Hardware abstraction layer (HAL)
* Use dependency injection via references or interfaces (non-virtual preferred)
* Prefer compile-time configuration over runtime setup
* **Assertion format:** Always `REQUIRE(<expected> == <actual>)` – put expected value first
* Tests may include `#include "pico/rand.h"`, `"pico/time.h"` etc., which are mocked

### Mocking Strategy

* Do NOT use mocking frameworks
* Use:

  * Simple fake structs with deterministic behavior
  * Manual stubs in `src/lib/mocks/` (pico.h, FreeRTOS.h, task.h, geomock.hpp, etc.)
  * Compile-time substitution (`#ifdef UNIT_TEST` only if unavoidable)
* Mock headers shadow Pico/FreeRTOS APIs; globals like `time_us_Value`, `mockConfig`, `geomock_*` are used by tests
* Example: `coreutils_test.cpp` sets `time_us_Value` to simulate time, calls `CoreUtils::msSinceEpoch()`, verifies result

Example pattern:

* Interface: `class GnsDriver { virtual void read() = 0; }`
* Production: `class L76B : public GnsDriver { /*Pico SDK calls*/ }`
* Test fake: `struct FakeGnss { void read() { /*deterministic data*/ } }`
* Test code: `void testParsing(GnsDriver& driver)` – inject FakeGnss

### Assertions

* Prefer `REQUIRE` for critical conditions
* Use `CHECK` for secondary validation
* Validate:

  * Boundary conditions
  * Error paths
  * Fixed-size buffer limits

### On-Target Tests (Only If Explicitly Requested)

* Keep tests minimal and non-interactive
* No test discovery at runtime
* No dynamic registration
* Tests must run deterministically at boot or via explicit call
* Output via UART only if explicitly allowed

---

## Documentation

* Comment code with embedded developers as the audience
* Explain hardware assumptions
* Document units (ms, us, ticks, bytes, bits)
* In tests, document *why* a case exists, not what the code does

---

## Code Generation Expectations

When generating code:

* Output production-quality embedded C++
* Keep code minimal and deterministic
* Favor clarity over cleverness
* Avoid “desktop C++” idioms
* If unsure, ask for clarification instead of guessing

If a requested feature violates these rules, explain why and propose a safe embedded alternative.
