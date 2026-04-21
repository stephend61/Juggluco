For more information see: https://www.juggluco.nl/Juggluco/cmdline/index.html

## Building

Compile for debugging with:

```sh
cmake -DDEBUG=on $srcdir
make juggluco
```

Replace `$srcdir` with the directory containing the C++ source of Juggluco (the directory that contains `CMakeLists.txt`).

To compile without logging and debug information (in a fresh directory):

```sh
cmake -DLOG=off $srcdir
make juggluco
```

Juggluco server can also function as Nightscout/xDrip web server, export data and show images, see:
https://www.juggluco.nl/Juggluco/webserver.html

SSL doesn't work with the statically linked precompiled version, use the dynamic precompiled version or compile it yourself.

To use the SSL version, you need an authenticated ssl key for the hostname that is used to contact with the juggluco server.

You can get such a key for your domain name for free using certbot (https://certbot.eff.org/instructions).

See Left menu->Settings->Exchange Data->Web server->Help in Juggluco or https://www.juggluco.nl/Jugglucohelp/Nightscouthelp.html

Put `fullchain.pem` and `privkey.pem` in the `jugglucodata` directory where also the other data is saved.

For more information see in Juggluco Left menu->Settings->Exchange Data->Web Server->Help.

## Optional features

### MQTT support (optional)

MQTT publishing support is optional and controlled by a CMake option.

- Default: OFF
- Enable: configure with `-DJUGGLUCO_MQTT=ON`
- Dependency: `libmosquitto` headers and library

When MQTT is disabled, the build uses a stub implementation so there is no hard dependency on `libmosquitto`.

#### MQTT topic + payload format

MQTT messages are published via [`mqtt::publish_json()`](mqtt/mqtt.hpp:170) to a topic template configured by `mqtt.topic`.

#### MQTT configuration file: `.mqttrc`

MQTT settings are read from a separate config file named `.mqttrc` (to avoid accidental overwrites when the data directory is set/changed).

- `.jugglucorc` remains dedicated to the **data directory** (historical behavior).
- `.mqttrc` contains **only** `key=value` lines for MQTT (currently `mqtt.*`).

#### Data directory configuration file: `.jugglucorc`

The data directory is configured separately via `.jugglucorc`.

- Format: first non-empty, non-comment line is the directory name/path.
- This file may be updated/overwritten by command-line options that change the data directory (e.g. `-d`).
- Keep MQTT settings out of this file; put them in `.mqttrc`.

Example `.mqttrc`:

```ini
# Enable/disable MQTT
mqtt.enabled=true

# Broker
mqtt.host=127.0.0.1
mqtt.port=1883

# Topic template (variables: {event}, {device})
mqtt.topic=juggluco/{event}

# Auth (optional)
#mqtt.username=juggluco
#mqtt.password=secret

# QoS: 0..2
mqtt.qos=2
```

- Default topic template: `juggluco/{event}`
- Template variables:
    - `{event}`: event name (see below)
    - `{device}`: device id (optional; use this for multi-instance setups)

Events currently published:

1) **`command`** (notification that a command was received)

Published when the `sglucose` command is processed in [`net/getcommand.cpp`](net/getcommand.cpp:246).

Example topic (default):

```
juggluco/command
```

Example payload:

```json
{
  "command": "sglucose",
  "type": 2
}
```

2) **`glucose`** (latest glucose point)

Published from [`processglucosevalue()`](cmdline/main.cpp:1019) when a fresh stream value is available.

3) **`status`** (startup notification)

Published once during startup from [`readconfig()`](cmdline/main.cpp:616) after MQTT init.

Example topic (default):

```
juggluco/glucose
```

Example payload:

```json
{
  "sensor_index": 2,
  "sensor_id": "1QBA5A04503",
  "time": 1713484800,
  "time_local": "2026-04-16 14:40:21",
  "tz": 10,
  "minute_index": 123456789,
  "raw_mgdl": 102,
  "raw_mmol": 5.7,
  "cal_mgdl": 100.4,
  "cal_mmol": 5.6,
  "cal_available": true,
  "change": 0.297296,
  "trend": 3
}
```

Field notes:

- `raw_mgdl` is always present (integer).
- `sensor_id` is the sensor's human-readable identifier string (matching the export's `Sensorid`).
- `time` is unix seconds; `time_local` is a local time string derived from the runtime's timezone.
- `tz` is the local timezone offset from UTC in hours (may be fractional for half-hour zones).
- `minute_index` is the per-sensor minute/sample index (the underlying stream point id), bounded by `maxminutes`.
- `raw_mmol`, `cal_mgdl`, and `cal_mmol` are rounded to 1 decimal place.
- `change` is formatted with 6 digits after the decimal point.
- `cal_mgdl` / `cal_mmol` are always present in MQTT for a stable schema. If a calibrated value cannot be computed, they fall back to the raw value and `cal_available` is `false`.
- `time` and `minute_index` are taken from the underlying stream point; types are numeric.
- `trend` is the underlying trend enum/value (`poll->tr`).

#### Installing `libmosquitto` (client library)

MQTT support uses the *client* library from the Eclipse Mosquitto project.

##### Linux

Install the development package (headers + library):

- **Debian / Ubuntu**

  ```sh
  sudo apt-get update
  sudo apt-get install -y libmosquitto-dev
  ```

- **Fedora / RHEL family**

  ```sh
  sudo dnf install -y mosquitto-devel
  ```

- **Arch Linux**

  ```sh
  sudo pacman -S --needed mosquitto
  ```

Then configure with MQTT enabled:

```sh
cmake -S . -B build -DJUGGLUCO_MQTT=ON
cmake --build build --target juggluco
```

##### Windows

Two common approaches:

1) **vcpkg (recommended for CMake builds)**

   ```powershell
   vcpkg install mosquitto
   ```

   Then configure CMake with the vcpkg toolchain:

   ```powershell
   cmake -S . -B build -DJUGGLUCO_MQTT=ON -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake
   cmake --build build --target juggluco
   ```

2) **MSYS2 (MinGW)**

   ```sh
   pacman -S --needed mingw-w64-x86_64-mosquitto
   ```

   In this case you may also need to ensure your CMake generator/compiler matches the MSYS2 toolchain you installed.

If CMake fails to find Mosquitto, it means it cannot locate `mosquitto.h` and/or the `mosquitto` library. The build system searches for these via `find_path()` / `find_library()` when [`JUGGLUCO_MQTT`](CMakeLists.txt:31) is ON.

## Repository / build hygiene notes

### Do not commit build outputs

This repo includes a `.gitignore` that ignores common build artifacts and IDE files. In particular, it ignores:

- CMake generator output (e.g. `CMakeFiles/`, `CMakeCache.txt`, `cmake_install.cmake`, `Makefile`)
- IDE metadata (e.g. `.idea/`, `.vscode/`)
- Local runtime data (`.jugglucorc`, `jugglucodata/`)
- Local runtime data (`.jugglucorc`, `.mqttrc`, `jugglucodata/`)

If you see CMake-generated files like `cmake_install.cmake`, `LibJuiceConfig*.cmake`, or generated `Makefile`s showing up as changes, it usually means CMake was run **in the source tree**. Prefer out-of-source builds (see below).

### Prefer out-of-source builds

Running CMake in a separate directory keeps generated files out of the source tree:

```sh
mkdir -p build
cmake -S . -B build -DDEBUG=on
cmake --build build --target juggluco
```

### Toolchain compatibility changes

Some code was adjusted to build on more common Linux toolchains:

- The build uses `-std=gnu++23` rather than `-std=gnu++26`.
- Some C++23 features that are not reliably supported everywhere (e.g. explicit object parameter / “deducing this”, and `std::ranges::contains_subrange`) were replaced with conventional/portable alternatives.

## Non-MQTT changes (what changed and why)

This section documents changes that were made **for build portability and repo hygiene**, not for the MQTT feature itself.

### 1) `.gitignore` + stop tracking generated artifacts

**What changed**

- Added [`/.gitignore`](.gitignore:1) to ignore build artifacts (CMake output, objects, libraries, binaries), IDE metadata, and local runtime data.
- Removed already-tracked build/IDE outputs from Git history going forward (commit `6360c6d`).
- Stopped tracking the generated file [`curve/arjugglucotext.cpp`](curve/arjugglucotext.cpp:1) and added it to ignore.

**Why**

- Build products like `CMakeFiles/`, `CMakeCache.txt`, object files, and generated `Makefile`s change constantly and should not be versioned. Keeping them out of Git makes diffs reviewable and avoids accidental commits.
- [`curve/arjugglucotext.cpp`](curve/arjugglucotext.cpp:1) is a generated output. It is produced by the CMake custom command declared in [`CMakeLists.txt`](CMakeLists.txt:88) through [`CMakeLists.txt:112`](CMakeLists.txt:112) (the `generate_arjugglucotext` target). Only the input template [`curve/arjugglucotext.in.cpp`](CMakeLists.txt:88) should be treated as source.

### 2) Make the build less dependent on bleeding-edge compiler support

#### 2.1 Use C++23 rather than `gnu++26`

**What changed**

- The default standard flag was changed from `-std=gnu++26` to `-std=gnu++23` in [`CMakeLists.txt`](CMakeLists.txt:35).

**Why**

- `gnu++26` is not widely supported on stable distro toolchains. Using C++23 keeps the code modern while improving the chance that a stock GCC/Clang will build the project.

#### 2.2 Fix a CMake flags bug when `sys/prctl.h` is missing

**What changed**

- Fixed an assignment that accidentally replaced `CMAKE_CXX_FLAGS` with `CMAKE_C_FLAGS` in the `HAVE_PRCTL` fallback path. See [`CMakeLists.txt:49`](CMakeLists.txt:49).

**Why**

- Overwriting `CMAKE_CXX_FLAGS` can drop the configured `-std=...` and other C++ flags, leading to confusing compile errors (templates, `requires`, `std::span`, etc.).

### 3) Replace toolchain-incomplete C++23 features with portable alternatives

#### 3.1 Replace C++23 “deducing this” (explicit object parameter)

**What changed**

Several headers used the C++23 explicit object parameter syntax (the `this Self&& self` form). This was replaced with conventional member functions and `const` overloads.

Examples:

- [`settings/settings.hpp`](settings/settings.hpp:419) (GlucoseMeter helpers)
- [`net/TCPConnect.hpp`](net/TCPConnect.hpp:40) (socket getter helpers)
- [`net/ICE/PlaceBuf.hpp`](net/ICE/PlaceBuf.hpp:64) (buffer `data()` / `operator[]`)
- [`datbackup.hpp`](datbackup.hpp:165) (connection accessor)

**Why**

- GCC 13 (common on Linux) does not reliably support this syntax even under `-std=gnu++23`, so removing it improves build compatibility without changing intended behavior.

#### 3.2 Replace `std::ranges::contains_subrange`

**What changed**

- Replaced `std::ranges::contains_subrange(...)` usage with `std::search(...)` in [`sensoren.hpp`](sensoren.hpp:728).

**Why**

- `std::ranges::contains_subrange` is a C++23 library facility and may be missing depending on libstdc++ version. `std::search` is widely available and sufficient here.

### 4) Fix missing standard header include

**What changed**

- Added `<format>` include in [`hostJson.cpp`](hostJson.cpp:27).

**Why**

- The file uses `std::format_to` (see [`hostJson.cpp:34`](hostJson.cpp:34)); some standard libraries require explicitly including `<format>`.

### 5) Notes about accidental CMake-generated files

If CMake is run **in the source tree**, it may generate extra files such as:

- [`cmake_install.cmake`](cmake_install.cmake:1)
- [`LibJuiceConfig.cmake`](LibJuiceConfig.cmake:1)
- [`LibJuiceConfigVersion.cmake`](LibJuiceConfigVersion.cmake:1)
- [`libjuice/Makefile`](libjuice/Makefile:1)
- [`libjuice/cmake_install.cmake`](libjuice/cmake_install.cmake:1)

These are CMake-generated outputs (see headers like “CMAKE generated file: DO NOT EDIT!” in [`libjuice/Makefile`](libjuice/Makefile:1)) and are typically not intended to be committed. Prefer an out-of-source build (example above) to avoid generating them in the repo root.
