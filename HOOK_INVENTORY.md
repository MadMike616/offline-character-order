# Hook Inventory — Offline Character Order

## Plugin metadata

| Field | Value |
|---|---|
| Plugin name | Offline Character Order |
| Plugin ID | offline-character-order |
| Release version | 0.9.37 |
| Inventory updated | 2026-11-08 |
| Source revision | v0.9.37 release tag (commit on main after v0.9.36) |
| D2R build(s) audited | D2R 3.3.93847; in-game validation reported for v0.9.36; v0.9.37 runtime not revalidated |
| D2RLoader SDK / Plugin ABI | D2RLoader PluginSDK 0.4.0, commit 03a5af9e0feb3f95f95e148e330e01f6635950df / Plugin ABI 4 |
| Audit status | Complete |

## Scope and method

Inspected plugin-owned files under src/ (plugin.cpp, plugin.rc, and sort_panel_layout.hpp), CMakeLists.txt, and the v0.9.36 main branch. Searched for direct hook and patch installation sites. The v0.9.37 SDK/API naming migration and Release build do not change hook sites.

## Hook sites

| Site ID | Type | Target | Address / identifier | Operation | Guard / validation | Source reference | Notes |
|---|---|---|---|---|---|---|---|
| H-01 | Inline | CharacterSelectEvent | RVA 0x61FDF0 | InstallInlineHook intercepts the character-selection panel event for list ordering and selection synchronization | Expected bytes are supplied through CharacterSelectEventExpected; live widget structures and vtables are validated before mutation | src/plugin.cpp — D2RLoaderLoadPlugin / CharacterSelectEventHook | Limited to supported Offline selector layouts; unchanged from v0.9.36. |
| H-02 | Win32 input-message | Offline selector keyboard messages, WH_KEYBOARD | N/A | SetWindowsHookExW installs CustomOrderKeyboardProc on the selector UI thread | Installed only while the Offline selector is active in Custom mode; explicitly unhooked during shutdown | src/plugin.cpp — InitializeCustomOrderKeyboardHook / ShutdownCustomOrderKeyboardHook | Captures Ctrl+W/S ordering input; unchanged from v0.9.36. |

## Known collision overlaps

| Other plugin | Shared target | Relationship | Evidence / notes |
|---|---|---|---|
| RuffDood Offline Character Order | CharacterSelectEvent, RVA 0x61FDF0 | Same inline entry | The workspace aggregate lists both plugins installing an inline hook at this event entry. |

## Audit notes

Source audit completed for v0.9.37. Release changes the SDK dependency to 0.4.0 / ABI 4 and rebuilds in Release configuration; no hook-site changes from v0.9.36. Runtime behavior has not been retested for v0.9.37. The workspace aggregate identifies RuffDood Offline Character Order as sharing CharacterSelectEvent RVA 0x61FDF0.
