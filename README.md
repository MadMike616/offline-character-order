# Offline Character Order

**Version 0.9.12** is a D2RLoader plugin that sorts the Offline character list in Diablo II: Resurrected. It changes the order of the character rows and their click targets, and keeps the preview and selection border synchronized. It does not edit character save files.

## Sort modes

Use the arrows above the character list to cycle through:

1. Most Recent
2. Level High to Low
3. Level Low to High
4. A to Z
5. Z to A
6. Class — Amazon, Assassin, Barbarian, Druid, Necromancer, Paladin, Sorceress, Warlock; highest level first within each class

Set `sort_mode` in `offline-character-order.toml` to one of `most_recent`, `level_descending`, `level_ascending`, `name_ascending`, `name_descending`, `class_level_descending`, or `custom`. Custom mode uses `custom_order`; unlisted names follow in their original game order. `native` is accepted as an alias for `most_recent`.

## Build

Requirements:

- Windows x64
- CMake 3.29 or newer
- Visual Studio 2022 or newer with the MSVC C++ toolchain and Windows SDK
- Network access during the first CMake configure, to fetch the pinned D2RLoader PluginSDK revision

From a Visual Studio Developer PowerShell in this directory:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --target offline_character_order
```

The DLL is written to `build/Release/d2rl-offline-character-order.dll`. The CMake file pins the SDK to commit `6eb8f8b6192868214706bd6d528c5294f2f551b7` from [D2RLoader/PluginSDK](https://github.com/D2RLoader/PluginSDK).

The `.sprite` files in `assets/native` are the UI resources embedded in the DLL at compile time.

## Install

Copy the DLL to either the global plugin folder:

```text
<D2R folder>/d2rloader/plugins/
```

or the active mod's plugin folder:

```text
<D2R folder>/mods/<mod name>/d2rloader/plugins/
```

Launch D2R through D2RLoader and open the Offline character selection screen. D2RLoader creates `d2rloader/config/offline-character-order.toml` the first time the plugin loads. The plugin is intended for the Offline list only and requires D2RLoader's ABI 4 resource, panel, shared-event, widget, and UI-thread services.

## Compatibility note

The plugin targets D2R 3.3.93847 under D2RLoader. Sorting, selection synchronization, arrow controls, and the 0.9.12 panel close on entering gameplay have been validated in-game. The character-list hook uses build-specific addresses and layout checks. If a game update changes those structures, the plugin is designed to reject unknown layouts rather than reorder an unvalidated list.
