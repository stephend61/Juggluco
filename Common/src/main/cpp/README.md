For more information see: https://www.juggluco.nl/Juggluco/cmdline/index.html

## Building

This directory contains the CMake project for the **desktop/server** command-line binary `juggluco`.

Run the following commands from [`Common/src/main/cpp/`](.:1).

### Debug build (logging enabled)

```sh
cmake -S . -B build -DDEBUG=ON -DLOG=ON
cmake --build build --target juggluco -j
```

### Release build (logging disabled)

```sh
cmake -S . -B build-release -DDEBUG=OFF -DLOG=OFF
cmake --build build-release --target juggluco -j
```

Notes:

- The `DEBUG` CMake option is declared in [`CMakeLists.txt`](CMakeLists.txt:4).
- `LOG` is treated as a CMake variable toggle: when it is OFF/empty, `-DNOLOG=1` is added in [`CMakeLists.txt`](CMakeLists.txt:14).
- The default C++ standard for the desktop build is currently set to `-std=gnu++26` in [`CMakeLists.txt`](CMakeLists.txt:38).
- Avoid setting `-DCMAKE_BUILD_TYPE=...` for this project: the top-level logic in [`CMakeLists.txt`](CMakeLists.txt:2) uses its own `DEBUG`/`LOG` switches.

## Runtime dependencies (running the `juggluco` executable)

The exact runtime dependencies depend on how you built the binary (static vs dynamic, MQTT enabled, etc.).

### Linux

On Linux, `juggluco` is typically dynamically linked against system libraries such as:

- OpenSSL (`libssl` + `libcrypto`) for HTTPS/SSL features
- zlib (`libz`)
- pthread (`libpthread`)
- `libm`, `libdl`, and the system C/C++ runtime

If you enabled MQTT (`-DJUGGLUCO_MQTT=ON`), you will also need:

- Eclipse Mosquitto client library (`libmosquitto`)

To see the exact list for your build:

```sh
ldd ./juggluco
```

### Windows

On Windows, you typically need to ship the required DLLs alongside `juggluco.exe` (or have them available in `PATH`).

Common runtime DLLs include:

- OpenSSL DLLs matching your build (e.g. `libssl-*.dll`, `libcrypto-*.dll`)
- If MQTT is enabled: `mosquitto.dll` (and any of its dependencies)

To see what your binary depends on:

- Visual Studio Developer Command Prompt:

  ```bat
  dumpbin /DEPENDENTS juggluco.exe
  ```

- Or with MinGW tools:

  ```sh
  objdump -p juggluco.exe | grep -i "DLL Name"
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

- Format: first non-empty, non-comment line **without `=`** is the directory name/path.
- This file may be updated/overwritten by command-line options that change the data directory (e.g. `-d`).
- Keep MQTT settings out of this file; put them in `.mqttrc`.

Example `.jugglucorc`:

```text
jugglucodata
```

Both `.jugglucorc` and `.mqttrc` are read from the **current working directory** (the directory you run `juggluco` from). See [`dirconf`](cmdline/main.cpp:315) / [`mqttconf`](cmdline/main.cpp:316).

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

Published from [`processglucosevalue()`](cmdline/main.cpp:1062) when a fresh stream value is available.

3) **`status`** (startup notification)

Published once during startup from [`readconfig()`](cmdline/main.cpp:356) after MQTT init.

Example topic (default):

```
juggluco/status
```

Example payload:

```json
{
  "status": "started"
}

This payload is currently fixed (it does not include timestamps or device id).
```

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

If CMake fails to find Mosquitto, it means it cannot locate `mosquitto.h` and/or the `mosquitto` library. The build system searches for these via `find_path()` / `find_library()` when [`JUGGLUCO_MQTT`](CMakeLists.txt:32) is ON.
