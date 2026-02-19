# AGENTS.md

## Project Overview

**AI Subscriptions Monitor** -- A lightweight native Windows desktop widget (C++17 / Win32 / GDI) that monitors AI subscription service usage and quotas in real time. It fetches JSON data from an HTTP API endpoint and renders color-coded progress bars.

- **Platform**: Windows 10 1607+ / Windows 11 only
- **Runtime dependencies**: None (single static executable)
- **Third-party code**: `nlohmann/json` v3.11.3 vendored as `src/json.hpp` -- do NOT modify this file

## Architecture

```
src/
  main.cpp           # Entry point, Win32 window framework, message loop, all UI state
  subscription.h/cpp # Data model structs, JSON deserialization (nlohmann/json from_json overloads)
  http_client.h/cpp  # Synchronous WinHTTP client wrapper (runs on background thread)
  renderer.h/cpp     # GDI progress bar rendering engine, theme/color schemes, scrollbar, DPI
  resource.h         # Win32 resource IDs (icons, version)
  app.rc             # Win32 resource script (icon embedding, version info)
  json.hpp           # Vendored nlohmann/json -- DO NOT EDIT
```

### Key Design Decisions

- **Single global `AppState`** (`g_app`): All application state lives in this struct. It is created in `wWinMain` and accessed via the global pointer.
- **Background HTTP thread**: `RefreshData()` spawns `g_refreshThread` to fetch data. Shared state (`subscriptions`, `lastError`, `contentHeight`) is protected by `g_dataMutex`. The background thread posts `WM_USER+1` to the UI thread when data is ready.
- **Double-buffered rendering**: `OnPaint` creates an off-screen memory DC, renders everything there, then `BitBlt`s to screen. No direct painting to the window DC.
- **No framework**: Pure Win32 API + GDI. No MFC, ATL, WTL, or any UI framework.
- **Static linking**: All CRT and system libraries are statically linked (`/MT` for Release, `/MTd` for Debug). The output is a single `.exe` with zero DLL dependencies.

## Build System

The sole build system is `build.ps1` (PowerShell). There is no CMakeLists.txt, no vcxproj.

```powershell
.\build.ps1                        # Release build
.\build.ps1 -Config Debug          # Debug build
.\build.ps1 -Clean                 # Clean + rebuild
.\build.ps1 -Run                   # Build and launch
.\build.ps1 -Analyze               # Release + MSVC static analysis
.\build.ps1 -Analyze -Config Debug # Debug + MSVC static analysis
.\build.ps1 -IconOnly              # Regenerate .ico files only
```

Output: `build\AISubscriptionsMonitor.exe`

### Build Requirements

- Visual Studio 2022 with C++ Desktop Development workload (provides `cl.exe`, `rc.exe`, `vcvars64.bat`)
- C++17 standard (`/std:c++17`)
- Linked libraries: `winhttp.lib gdi32.lib user32.lib kernel32.lib comctl32.lib advapi32.lib dwmapi.lib shell32.lib ole32.lib`

### Icon Generation

ICO files are auto-generated from PNG sources via Python/Pillow. If Python is unavailable, existing `.ico` files are used as-is.

- `app_icon.png` -> `app_icon.ico` (light-on-dark, for dark mode)
- `app_icon_light.png` -> `app_icon_light.ico` (dark-on-light, for light mode)

## Code Style & Conventions

### Formatting

Configured via `.clang-format`:

- **Based on**: Microsoft style
- **Indent**: 4 spaces (no tabs)
- **Line limit**: 120 columns
- **Braces**: Allman for functions/classes/enums, K&R for control statements
- **Pointer/ref alignment**: Left (`int* ptr`, not `int *ptr`)
- **Include order**: Windows headers first, then standard library, then project headers

### Naming (from `.clang-tidy`)

| Element | Convention | Example |
|---------|-----------|---------|
| Classes/Structs | CamelCase | `AppState`, `HttpClient` |
| Enums | CamelCase | `ThemeMode`, `ScrollHitZone` |
| Enum constants | UPPER_CASE | `IDM_TRAY_SHOW` |
| Functions | CamelCase | `RefreshData()`, `OnPaint()` |
| Global constants | k + UPPER_CASE | `kWindowWidth`, `kScrollLineHeight` |
| Member variables | lower_case + trailing underscore | `windowWidth_`, `hFontBold_` |
| Local variables | lower_case | `client_height`, `scroll_off` |
| Parameters | lower_case | `scroll_offset`, `mouse_y` |
| Macros | UPPER_CASE | `WM_TRAYICON`, `DWMWA_USE_IMMERSIVE_DARK_MODE` |

### GDI Resource Management

- Every `CreateFont`, `CreateSolidBrush`, `CreateCompatibleDC`, `CreateCompatibleBitmap` must have a matching `DeleteObject`/`DeleteDC`
- Prefer caching GDI objects (fonts, brushes) over create-per-frame
- Always `SelectObject` the old object back before deleting

### Thread Safety

- `g_dataMutex` protects: `subscriptions`, `lastError`, `contentHeight`, `scrollOffset`
- `g_logMutex` protects: `g_logFile`, `g_consoleOut`
- `g_app->isLoading` is `std::atomic<bool>` -- read without mutex
- Background thread must NOT call Win32 UI functions directly; use `PostMessage` to notify UI thread
- Signal/exception handlers must only use async-signal-safe operations (`_write`, `_exit`)

## Module Responsibilities

### main.cpp (~1800 lines)

The monolith. Contains:
- `wWinMain` entry point and command-line parsing
- Window class registration, creation, message loop
- `WndProc` message handler (WM_PAINT, WM_SIZE, WM_MOUSEWHEEL, WM_TRAYICON, etc.)
- Settings persistence (`LoadSettings` / `SaveSettings` to `%LOCALAPPDATA%\AISubscriptionsMonitor\settings.txt`)
- URL parsing (`ParseUrl`)
- System tray management
- Compact mode / theme toggling
- Logging system (`Log()`, `InitLogging()`, `CloseLogging()`, log rotation)
- Crash handlers (`CrashHandler`, `ExceptionHandler`)
- Background data refresh (`RefreshData` spawns thread)

### subscription.h/cpp

- Data model: `Subscription > Metric > Amount/Window`, plus `Plan`, `Cost`
- All `from_json` overloads are `inline` in the header
- `ParseSubscriptions()` parses a JSON array string into `vector<Subscription>`
- `json_to_double()` helper handles both numeric and string-encoded numbers

### http_client.h/cpp

- `HttpClient` class wrapping WinHTTP
- `GetSync()` is blocking (called from background thread)
- RAII `HandleGuard` for WinHTTP handles
- 10 MB response size limit (`kMaxResponseSize`)
- Supports HTTP and HTTPS

### renderer.h/cpp

- `ProgressBarRenderer` class owns all rendering logic
- `Render()` draws subscriptions with scroll offset, returns content height
- `CalculateContentHeight()` / `CalculateContentWidth()` for layout without rendering
- Custom scrollbar: `DrawScrollbar()`, `HitTestScrollbar()`, `ScrollOffsetFromThumbDrag()`
- Theme support: `ColorScheme` struct, `GetLightScheme()` / `GetDarkScheme()`
- `ThemeMode` enum: `Light`, `Dark`, `System`
- `IsSystemDarkMode()` reads Windows registry
- DPI-aware font creation (`OnDpiChanged`, `dpiScale_`)
- Compact mode: smaller fonts, tighter spacing, different layout

## Common Tasks

### Adding a new metric field

1. Add the field to the relevant struct in `subscription.h`
2. Update the corresponding `from_json` overload in `subscription.h`
3. Update rendering in `renderer.cpp` (`RenderMetric` or `RenderServiceHeader`)
4. Update `CalculateContentHeight` if the new field affects layout

### Adding a new context menu item

1. Define a new command ID in `main.cpp` (use `0xE0xx` range for system menu items, `400xx` for popup menu items)
2. Add to `BuildContextMenu()` in `main.cpp`
3. Handle in `WndProc` under `WM_COMMAND` or `WM_SYSCOMMAND`
4. If persistent, add to `SaveSettings`/`LoadSettings`

### Changing theme colors

Edit `kLightScheme` or `kDarkScheme` in `renderer.cpp`. The `ColorScheme` struct defines all color slots.

### Adding a new setting

1. Add field to `SavedSettings` struct in `main.cpp`
2. Write it in `SaveSettings()`
3. Read it in `LoadSettings()`
4. Apply it during window creation in `wWinMain`

## Testing

There is no automated test suite. Verification is manual:
- Build with `.\build.ps1 -Config Debug -Run`
- Use `--debug` flag for console output and file logging
- Check `%LOCALAPPDATA%\AISubscriptionsMonitor\AISubscriptionsMonitor.log`

## Code Review & Fix Convergence

Multi-round review must converge — each round should find fewer and less severe issues. Follow this protocol to prevent oscillation.

### Review Order: Top-Down by Severity Layer

Each round focuses on ONE layer. Do not mix layers — fixing a higher layer changes code structure and may eliminate lower-layer issues automatically.

| Round | Layer | What to look for | Tools |
|-------|-------|-------------------|-------|
| 1 | Correctness | Thread safety, crashes, data races, undefined behavior | `.\build.ps1 -Analyze`, manual review |
| 2 | Robustness | Error handling, resource leaks, boundary conditions, OOM | `.\build.ps1 -Analyze`, Task Manager GDI handle count |
| 3 | Quality | Dead code, duplication, naming, magic numbers | `clang-tidy`, manual review |

### Rules

1. **Automate first, review second.** Run `.\build.ps1 -Analyze` before each round. Issues that tools can find should never appear in manual review.
2. **One fix per commit.** Never bundle a thread-safety fix with a cosmetic rename. Bundled fixes hide regressions.
3. **Review only the diff after each round.** After fixing Round N issues, re-review only the changed lines from the perspective of Round N's layer. Do not re-scan the entire codebase — that starts a new round.
4. **A fix must not introduce issues at its own layer or above.** A Round 2 fix (robustness) must not break Round 1 guarantees (correctness). If it does, that's a regression, not a new finding.
5. **Stop when the round finds nothing at Critical/High.** If a round produces only Low-severity findings, fix them and stop. Do not start another round looking for more.

### Convergence Check

After each round, count findings by severity:

- **Converging**: Round N finds fewer issues than Round N-1, and max severity is equal or lower.
- **Diverging**: Round N finds more issues or higher severity than Round N-1. This means fixes are introducing regressions — stop, revert, and re-examine the approach.

### Available Tooling

```powershell
# MSVC static analysis (built into build.ps1)
.\build.ps1 -Analyze                  # Release + /analyze
.\build.ps1 -Analyze -Config Debug    # Debug + /analyze

# Clang-Tidy (if available; .clang-tidy already configured)
clang-tidy src/main.cpp src/renderer.cpp src/subscription.cpp src/http_client.cpp -- -std=c++17 -DUNICODE -D_UNICODE -I src

# Runtime: GDI leak detection
# Task Manager > Details > Add Column "GDI Objects" — value must stay stable over time

# Runtime: memory errors (if installed)
# drmemory -- build\AISubscriptionsMonitor.exe
```

## Important Warnings

- **DO NOT** modify `src/json.hpp` -- it is a vendored third-party header
- **DO NOT** add a CMakeLists.txt or any other build system -- `build.ps1` is the sole build tool
- **DO NOT** use `/MD` (dynamic CRT) -- the project uses `/MT` (static CRT) for zero-dependency deployment
- **DO NOT** call UI functions from the background HTTP thread -- always `PostMessage`
- **DO NOT** access shared state without `g_dataMutex` from the background thread
- **DO NOT** use `as any` equivalents like `reinterpret_cast` without strong justification
- **DO NOT** introduce new third-party dependencies -- this is a zero-dependency native app
- Log file rotation at 5 MB; old log renamed to `.log.old`
- Settings file writes are atomic (write to `.tmp`, then `MoveFileExW` replace)
