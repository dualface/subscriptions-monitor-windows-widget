# AI Subscriptions Monitor

A lightweight native Windows desktop widget for monitoring AI subscription service usage and quotas in real time. It fetches data from an HTTP API endpoint and renders color-coded progress bars showing consumption per service/metric.

![Windows](https://img.shields.io/badge/platform-Windows%2010%2B-0078D6?logo=windows)
![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus)
![License](https://img.shields.io/badge/license-MIT-green)

## Features

- **Real-time monitoring** -- Fetches subscription data from a configurable HTTP API endpoint
- **Color-coded progress bars** -- Green (< 50%), yellow (50-80%), red (> 80%) usage indicators
- **Auto-refresh** -- Automatically refreshes every 60 seconds, with manual refresh via F5
- **Compact mode** -- Minimalist view with smaller bars and condensed information
- **Theme support** -- Light, dark, or system theme with automatic switching
- **Always on top** -- Pin window to stay above other applications
- **System tray** -- Minimize to system tray, show/hide from tray menu
- **Window transparency** -- Adjustable opacity (25%, 50%, 75%, 100%)
- **HiDPI support** -- Per-Monitor DPI awareness (Windows 10 1607+), scales cleanly on high-DPI displays
- **Persistent settings** -- Saves window position, size, theme, API URL, and pin state between sessions
- **Flexible URL input** -- Command-line argument, saved config, or interactive dialog on first run
- **Smooth scrolling** -- Mouse wheel support with custom scrollbar
- **Debug mode** -- Optional `--debug` flag enables console output and file logging
- **Smart formatting** -- Large numbers displayed with K/M suffixes; countdown to quota reset shown as "Xh Ym"
- **Lightweight** -- Pure native Win32 + GDI, no Electron or web runtime; single static executable with zero runtime dependencies
- **Double-buffered rendering** -- Flicker-free drawing

## Screenshot

### Normal Mode
```
┌─────────────────────────────────────────────┐
│  ZenMux - Ultra Plan                        │
│                                             │
│  7d Flows (7 Day)              64.4%        │
│  ██████████████░░░░░░░░  772.66 / 1200      │
│                          48h 23m refresh    │
│                                             │
│  Daily Tokens (1 Day)          23.1%        │
│  █████░░░░░░░░░░░░░░░░  231K / 1M           │
│                          8h 12m refresh     │
└─────────────────────────────────────────────┘
```

### Compact Mode
```
┌──────────────────┐
│ ZenMux           │
│ 7 Day    64%     │
│ 1 Day    23%     │
└──────────────────┘
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
├── build.ps1                 # PowerShell build script
├── app_icon.png              # Application icon source
├── app_icon.ico              # Generated icon (multi-size)
├── LICENSE                   # MIT License
├── README.md
└── src/
    ├── main.cpp              # Win32 window framework, message loop, entry point
    ├── subscription.h/cpp    # Data model, JSON deserialization, formatting
    ├── http_client.h/cpp     # WinHTTP client wrapper
    ├── renderer.h/cpp        # GDI progress bar rendering engine
    ├── resource.h            # Win32 resource IDs
    ├── app.rc                # Win32 resource script (icon embedding)
    └── json.hpp              # nlohmann/json v3.11.3 (header-only, vendored)
```

## Building

### Option A: build.ps1 (recommended)

The PowerShell build script auto-detects your Visual Studio installation, generates the icon, and compiles everything in one step.

```powershell
# Default Release build
.\build.ps1

# Debug build (with debug symbols)
.\build.ps1 -Config Debug

# Clean build directory, then rebuild
.\build.ps1 -Clean

# Build and launch the app
.\build.ps1 -Run

# Only regenerate app_icon.ico from app_icon.png
.\build.ps1 -IconOnly

# Combine flags
.\build.ps1 -Clean -Config Release -Run
```

The executable will be at `build\AISubscriptionsMonitor.exe`.

> **Note:** If you encounter an execution policy error, run:
> ```powershell
> powershell -ExecutionPolicy Bypass -File build.ps1
> ```

> **Note:** ICO auto-generation requires Python 3 with [Pillow](https://pypi.org/project/Pillow/) (`pip install Pillow`). If unavailable, the script will skip this step and use the existing `app_icon.ico`.

### Option B: CMake

```powershell
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022"
cmake --build . --config Release
```

The executable will be at `build/bin/Release/AISubscriptionsMonitor.exe`.

### Option C: Visual Studio IDE

1. Open Visual Studio 2022
2. Create a new empty C++ project
3. Add all files from `src/` to the project
4. Set project properties:
   - **C/C++ > C++ Language Standard**: ISO C++17
   - **C/C++ > Additional Include Directories**: `$(ProjectDir)src`
   - **Linker > Input > Additional Dependencies**: `winhttp.lib;gdi32.lib;user32.lib;kernel32.lib;comctl32.lib;advapi32.lib;dwmapi.lib;shell32.lib;ole32.lib`
   - **Linker > System > SubSystem**: Windows
5. Build and run

## Usage

```
AISubscriptionsMonitor.exe [--debug] [--theme <theme>] [--compact] [<url>]
```

| Argument      | Description                                                      |
| ------------- | ---------------------------------------------------------------- |
| `<url>`       | HTTP endpoint that returns subscription data (optional if saved) |
| `--debug`     | Enable debug console and file logging (`AISubscriptionsMonitor.log`) |
| `--theme`     | Theme mode: `light`, `dark`, or `system` (default: `system`)     |
| `--compact`   | Start in compact mode                                            |

### Examples

```powershell
# Basic usage with URL
AISubscriptionsMonitor.exe http://api.example.com:8080/subscriptions

# With debug logging
AISubscriptionsMonitor.exe --debug http://localhost:3000/api/v1/usage

# Dark theme
AISubscriptionsMonitor.exe --theme dark http://api.example.com/subscriptions

# Compact mode with light theme
AISubscriptionsMonitor.exe --compact --theme light http://api.example.com/subscriptions
```

### First Run

If no URL is provided via command line or saved in configuration, the app will prompt you with an input dialog to enter the API endpoint. The URL will be validated before the app starts.

### System Tray

The application runs from the system tray:
- **Left click** tray icon: Show/hide the window
- **Right click** tray icon: Open context menu
- **Minimize**: Hides to tray instead of taskbar
- **Close button (X)**: Hides to tray (use Exit in menu to quit)

### Context Menu (Right-click on window)

- **Hide to Tray** -- Minimize to system tray
- **Compact Mode** -- Toggle between normal and compact view
- **Pin to Top** -- Keep window always on top
- **Opacity** -- Set window transparency (25%, 50%, 75%, 100%)
- **Exit** -- Close the application

### Keyboard Shortcuts

| Key          | Action                  |
| ------------ | ----------------------- |
| `F5`         | Manual data refresh     |
| `Ctrl + T`   | Toggle "Pin to Top"     |

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
        }
      }
    ],
    "cost": {
      "total": 12.50,
      "currency": "USD"
    },
    "status": "ok"
  }
]
```

### Field Reference

| Field                       | Type     | Required | Description                              |
| --------------------------- | -------- | -------- | ---------------------------------------- |
| `provider_id`               | string   | yes      | Unique provider identifier               |
| `display_name`              | string   | yes      | Human-readable service name              |
| `plan.name`                 | string   | yes      | Subscription plan name                   |
| `plan.type`                 | string   | yes      | Plan type (e.g. `"subscription"`)        |
| `metrics[].name`            | string   | yes      | Metric name (e.g. `"7d Flows"`)         |
| `metrics[].window.label`    | string   | yes      | Window label (e.g. `"7 Day"`)           |
| `metrics[].window.resets_at`| string   | no       | ISO 8601 timestamp for next reset        |
| `metrics[].amount.used`     | number   | yes      | Current usage                            |
| `metrics[].amount.limit`    | number   | no       | Usage cap (omit for unlimited metrics)   |
| `metrics[].amount.remaining`| number   | no       | Remaining quota                          |
| `metrics[].amount.unit`     | string   | yes      | Unit label (e.g. `"flows"`, `"tokens"`)  |
| `cost.total`                | number   | no       | Optional total cost                      |
| `cost.currency`             | string   | no       | Currency code (e.g. `"USD"`)             |

### Protocol Support

The app supports both HTTP and HTTPS endpoints:
- `http://host:port/path`
- `https://host:port/path`

Default ports: HTTP (80), HTTPS (443)

## Progress Bar Colors

### Light Theme
| Color  | Usage Range | Meaning              |
| ------ | ----------- | -------------------- |
| Green  | 0 -- 49%    | Normal usage         |
| Yellow | 50 -- 80%   | Elevated usage       |
| Red    | 81 -- 100%  | Approaching limit    |

### Dark Theme
| Color        | Usage Range | Meaning              |
| ------------ | ----------- | -------------------- |
| Muted Green  | 0 -- 49%    | Normal usage         |
| Orange       | 50 -- 80%   | Elevated usage       |
| Red          | 81 -- 100%  | Approaching limit    |

Metrics without a `limit` field are displayed as text-only (no progress bar).

## Configuration

Settings are automatically saved to `%LOCALAPPDATA%\AISubscriptionsMonitor\settings.txt`:

- Window position and size (separate for normal and compact modes)
- Pin state
- Theme preference
- API URL
- Opacity level

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

## Troubleshooting

### No window appears
Check the log file `AISubscriptionsMonitor.log` for errors.

### API connection fails
- Verify the URL is correct and accessible
- Check if firewall is blocking the connection
- For HTTPS, ensure the certificate is valid

### Wrong theme
The app follows the Windows system theme by default. Use `--theme light` or `--theme dark` to override.

## License

[MIT](LICENSE) -- Copyright (c) 2026 dualface
