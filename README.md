# AI Subscription Monitor

A lightweight native Windows desktop widget for monitoring AI subscription service usage and quotas in real time. It fetches data from an HTTP API endpoint and renders color-coded progress bars showing consumption per service/metric.

![Windows](https://img.shields.io/badge/platform-Windows%2010%2B-0078D6?logo=windows)
![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus)
![License](https://img.shields.io/badge/license-MIT-green)

## Features

- **Real-time monitoring** -- Fetches subscription data from a configurable HTTP API endpoint
- **Color-coded progress bars** -- Green (< 50%), yellow (50-80%), red (> 80%) usage indicators
- **Auto-refresh** -- Automatically refreshes every 60 seconds, with manual refresh via F5
- **HiDPI support** -- Per-Monitor DPI awareness (Windows 10 1607+), scales cleanly on high-DPI displays
- **Debug mode** -- Optional `--debug` flag enables console output and file logging
- **Smart formatting** -- Large numbers displayed with K/M suffixes; countdown to quota reset shown as "Xh Ym"
- **Lightweight** -- Pure native Win32 + GDI, no Electron or web runtime; single static executable with zero runtime dependencies
- **Double-buffered rendering** -- Flicker-free drawing

## Screenshot

```
┌─────────────────────────────────────────────┐
│  ZenMux - Ultra Plan                        │
│                                             │
│  7d Flows (7 Day)              64.4%        │
│  ██████████████░░░░░░░░  772.66 / 1200      │
│                          48h 23m refresh    │
│                                             │
│  Daily Tokens (1 Day)          23.1%        │
│  █████░░░░░░░░░░░░░░░░  231K / 1M          │
│                          8h 12m refresh     │
└─────────────────────────────────────────────┘
```

## Requirements

- **OS**: Windows 10 (version 1607+) or Windows 11
- **Build tools** (one of):
  - [CMake](https://cmake.org/) 3.16+ with Visual Studio 2022 generator
  - Visual Studio 2022 (Community or higher) with C++ Desktop Development workload
- **Dependencies**: None beyond the Windows SDK. The only third-party library ([nlohmann/json](https://github.com/nlohmann/json) v3.11.3) is vendored as a single header file.

## Project Structure

```
.
├── CMakeLists.txt            # CMake build configuration
├── build.bat                 # One-click build script (cl.exe)
├── LICENSE                   # MIT License
├── README.md
└── src/
    ├── main.cpp              # Win32 window framework, message loop, entry point
    ├── subscription.h/cpp    # Data model, JSON deserialization, formatting
    ├── http_client.h/cpp     # WinHTTP client wrapper
    ├── renderer.h/cpp        # GDI progress bar rendering engine
    └── json.hpp              # nlohmann/json v3.11.3 (header-only, vendored)
```

## Building

### Option A: CMake (recommended)

```powershell
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022"
cmake --build . --config Release
```

The executable will be at `build/bin/Release/AISubscriptionMonitor.exe`.

### Option B: build.bat

Run the batch file from the project root. It invokes `vcvars64.bat` and compiles directly with `cl.exe`:

```powershell
.\build.bat
```

The executable will be at `build\AISubscriptionMonitor.exe`.

> **Note:** The batch file assumes Visual Studio 2022 Community Edition is installed at the default path. Edit the `vcvars64.bat` path in `build.bat` if your installation differs.

### Option C: Visual Studio IDE

1. Open Visual Studio 2022
2. Create a new empty C++ project
3. Add all files from `src/` to the project
4. Set project properties:
   - **C/C++ > C++ Language Standard**: ISO C++17
   - **C/C++ > Additional Include Directories**: `$(ProjectDir)src`
   - **Linker > Input > Additional Dependencies**: `winhttp.lib;gdi32.lib;user32.lib;kernel32.lib;comctl32.lib`
   - **Linker > System > SubSystem**: Windows
5. Build and run

## Usage

```
AISubscriptionMonitor.exe [--debug] <url>
```

| Argument    | Description                                                      |
| ----------- | ---------------------------------------------------------------- |
| `<url>`     | HTTP endpoint that returns subscription data (required)          |
| `--debug`   | Enable debug console and file logging (`AISubscriptionMonitor.log`) |

### Examples

```powershell
# Basic usage
AISubscriptionMonitor.exe http://api.example.com:8080/subscriptions

# With debug logging
AISubscriptionMonitor.exe --debug http://localhost:3000/api/v1/usage
```

### Keyboard Shortcuts

| Key   | Action              |
| ----- | ------------------- |
| `F5`  | Manual data refresh |

## API Format

The endpoint must return a JSON array of subscription objects. Each subscription contains a provider, plan info, and one or more metrics with usage amounts:

```json
[
  {
    "provider_id": "zenmux",
    "display_name": "ZenMux",
    "name": "ZenMux",
    "timestamp": "2026-02-17T13:44:27.892203022+08:00",
    "plan": {
      "name": "Ultra Plan",
      "type": "subscription"
    },
    "metrics": [
      {
        "name": "7d Flows",
        "window": {
          "id": "7d",
          "label": "7 Day",
          "resets_at": "2026-02-20T07:36:05Z"
        },
        "amount": {
          "used": 772.66,
          "limit": 1200,
          "remaining": 427.34,
          "unit": "flows"
        },
        "cost": {
          "amount": 12.50,
          "currency": "USD"
        }
      }
    ],
    "status": "ok"
  }
]
```

### Field Reference

| Field                     | Type     | Required | Description                              |
| ------------------------- | -------- | -------- | ---------------------------------------- |
| `provider_id`             | string   | yes      | Unique provider identifier               |
| `display_name`            | string   | yes      | Human-readable service name              |
| `plan.name`               | string   | yes      | Subscription plan name                   |
| `plan.type`               | string   | yes      | Plan type (e.g. `"subscription"`)        |
| `metrics[].name`          | string   | yes      | Metric name (e.g. `"7d Flows"`)         |
| `metrics[].window.label`  | string   | yes      | Window label (e.g. `"7 Day"`)           |
| `metrics[].window.resets_at` | string | no    | ISO 8601 timestamp for next reset        |
| `metrics[].amount.used`   | number   | yes      | Current usage                            |
| `metrics[].amount.limit`  | number   | no       | Usage cap (omit for unlimited metrics)   |
| `metrics[].amount.remaining` | number | no    | Remaining quota                          |
| `metrics[].amount.unit`   | string   | yes      | Unit label (e.g. `"flows"`, `"tokens"`)  |
| `metrics[].cost`          | object   | no       | Optional cost information                |

## Progress Bar Colors

| Color  | Usage Range | Meaning              |
| ------ | ----------- | -------------------- |
| Green  | 0 -- 49%    | Normal usage         |
| Yellow | 50 -- 80%   | Elevated usage       |
| Red    | 81 -- 100%  | Approaching limit    |

Metrics without a `limit` field are displayed as text-only (no progress bar).

## Tech Stack

| Component   | Technology                                          |
| ----------- | --------------------------------------------------- |
| Language    | C++17                                               |
| Platform    | Win32 API                                           |
| UI          | Native GDI rendering                                |
| HTTP        | WinHTTP                                             |
| JSON        | [nlohmann/json](https://github.com/nlohmann/json) v3.11.3 |
| Build       | CMake 3.16+ / MSVC                                  |
| Font        | Microsoft YaHei UI (CJK-compatible)                 |

## License

[MIT](LICENSE) -- Copyright (c) 2026 dualface
