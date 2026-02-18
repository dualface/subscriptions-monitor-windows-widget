# Fix TODO List

Based on comprehensive code review. Ordered by severity.

## Round 1

### Critical

- [x] 1. Fix thread safety: move `CalculateContentHeight` call to UI thread instead of background thread
- [x] 2. Fix thread safety: protect `g_app` access after mutex unlock in background thread
- [x] 3. Fix potential UI hang: set HTTP timeouts after `HttpClient::Initialize()`

### High

- [x] 4. Remove deprecated `IsBadStringPtrW` usage in `WM_SETTINGCHANGE`
- [x] 5. Fix log file path: use `%LOCALAPPDATA%` instead of relative path
- [x] 6. Fix icon loading: use proper small icon size via `LoadImage`
- [x] 7. Fix `SavedSettings` uninitialized members (`isCompact`, `pinned`)

### Medium

- [x] 8. Remove duplicate `CreateSystemFont` function (main.cpp vs renderer.cpp)
- [x] 9. Add null check for `g_app` in `WM_MOUSEWHEEL` handler
- [x] 10. Protect `contentHeight`/`scrollOffset` reads in `WM_LBUTTONDOWN` with mutex
- [x] 11. Restrict window drag to compact mode only
- [x] 12. Remove duplicate `FormatNumber` (subscription.cpp vs renderer.cpp)

### Low

- [x] 13. Update `.gitignore` for `nul` file artefact
- [x] 14. Remove unused `#include <iostream>`
- [x] 15. Replace magic numbers with named constants

## Round 2

### Critical

- [x] 16. Fix thread safety: `WM_MOUSEMOVE` reads `contentHeight`/`scrollOffset` without lock
- [x] 17. Fix thread safety: `ScrollTo` and `UpdateScrollInfo` write `scrollOffset` without lock
- [x] 18. Fix COM lifecycle: `CoUninitialize()` called before `AppState` destructor releases `ITaskbarList`

### High

- [x] 19. Add `g_logMutex` to make `Log()` thread-safe (background + UI thread both call it)
- [x] 20. Replace non-async-signal-safe calls in `CrashHandler` with raw `_write()` to stderr
- [x] 21. Improve `ParseSubscriptions` error messages with per-entry context
- [x] 22. Add missing `ITaskbarList::HrInit()` call after `CoCreateInstance`

### Medium

- [x] 23. Remove unused `DLGTEMPLATE` struct and dead comments in `ShowUrlInputDialog`
- [x] 24. Make `SaveSettings` atomic via write-to-temp + `MoveFileExW` replace
- [x] 25. Reuse cached `hBgBrush` in `OnPaint` instead of creating/deleting each frame
- [x] 26. Fix `DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2` macro spacing ambiguity
- [x] 27. Add overflow protection in `FormatNumber` for extreme `double` values

### Low

- [x] 28. Sanitize API URLs in log output to prevent credential leakage
- [x] 29. Add type-tolerant `json_to_double()` helper for `Amount` fields (handles string numbers)

## Round 3

### Critical

- [x] 30. Fix `ExceptionHandler`: replace `Log()`/`CloseLogging()` with async-safe `_write()` to stderr
- [x] 31. Fix `RefreshData`: replace blocking `join()` with `detach()` to avoid UI thread hang; add bounded wait in `WM_DESTROY`

### High

- [x] 32. Fix `WM_SYSCOMMAND` custom IDs: `IDM_PIN_TO_TOP` (0x0010) and `IDM_COMPACT_MODE` (40003) conflicted with system command range; moved to 0xE000+ range
- [x] 33. Cache GDI brushes in `ProgressBarRenderer` instead of `CreateSolidBrush`/`DeleteObject` per frame (bar bg, fill colors, scrollbar)
- [x] 34. Add HTTP response size limit (10 MB) in `HttpClient::GetSync` to prevent OOM from oversized responses

### Medium

- [x] 35. Fix `Window::formatResetTime()`: output contained ISO 8601 `T` separator instead of space
- [x] 36. Improve `ParseUrl`: add port range validation (1-65535), IPv6 literal support, fragment stripping
- [x] 37. Remove redundant `g_logFile.flush()` after `std::endl` (which already flushes)
- [x] 38. Fix `Render()` return value: add back `scrollOffset` to return true content height

### Low

- [x] 39. Fix `build.ps1` Debug CRT mismatch: change `/MDd` to `/MTd` for static linking consistency
- [x] 40. Remove `CMakeLists.txt`: keep `build.ps1` as the sole build system
