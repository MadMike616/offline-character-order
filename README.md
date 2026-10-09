# Offline Character Order

**Version 0.9.35** is a D2RLoader plugin that sorts the Offline character list in Diablo II: Resurrected. D2RLoader lists the author as MadMike. It changes the order of the character rows and their click targets, and keeps the preview and selection border synchronized. It does not edit character save files. The sort panel hides while modal overlays are open and closes when navigating away from character select.

## Sort modes

Use the arrows above the character list to cycle through:

1. Most Recent
2. Level High to Low
3. Level Low to High
4. A to Z
5. Z to A
6. Class — Amazon, Assassin, Barbarian, Druid, Necromancer, Paladin, Sorceress, Warlock; highest level first within each class
7. Custom

In Custom mode, select a character and press **Ctrl+W** to move it up or **Ctrl+S** to move it down one row. The top-to-bottom order is saved to `custom_order` in the final `[custom]` section, with one character name per TOML line and its row number in a comment. After the Offline list settles when opened or rebuilt, the plugin removes deleted names and duplicate entries from the saved order. Names not listed follow the game's original order. `native` is accepted as an alias for `most_recent`.

## UI layout configuration

Edit the `[ui]` section in `d2rloader/config/offline-character-order.toml`, then restart D2RLoader. The plaque, arrows, and mode label have independent layout-unit positions and sizes relative to the plugin panel anchor. Button positions and explicit dimensions affect both the arrow artwork and its hitbox. Arrow width or height set to `0` keeps that sprite dimension at its authored size.

```toml
# Plaque position and size.
[ui]
plaque_x = 14
plaque_y = 0
plaque_width = 72
plaque_height = 14

# Arrow positions. A width or height of 0 keeps that sprite's authored size.
left_arrow_x = -88
left_arrow_y = -13
left_arrow_width = 0
left_arrow_height = 0
right_arrow_x = 365
right_arrow_y = -13
right_arrow_width = 0
right_arrow_height = 0

# Mode label position, size, and D2R font settings.
label_x = 160
label_y = 28
label_width = 72
label_height = 14
label_font_face = ""
label_font_size = 0

# Keep the actively changing character order at the end of the file.
[custom]
custom_order = []
```

Change `left_arrow_*` and `right_arrow_*` independently to position or resize either button. Adjust `label_*` if you move or resize the plaque and want the text to follow it. `label_font_face` is a D2R font-face name, such as `BlizzardGlobal`, `Exocet`, or `Formal`; leave it blank to retain the current default. Windows-installed fonts such as Arial are not loaded automatically by D2R, so entering their names alone falls back to the game default. Set `label_font_size` to a point size from 1 to 200, or `0` to retain the current default.

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

Launch D2R through D2RLoader and open the Offline character selection screen. D2RLoader creates `d2rloader/config/offline-character-order.toml` the first time the plugin loads. The plugin is intended for the Offline list only and uses D2RLoader's ABI 4 resource, panel, shared-event, widget, and UI-thread services. Ctrl+W/S are captured only while the Offline character selector is active and Custom mode is selected.

## Compatibility note

The plugin was validated with D2R 3.3.93847 under D2RLoader. Version 0.9.30 removes the explicit executable build-fingerprint precheck; the author confirmed it works with both the non-Steam and Steam versions. The hook still asks D2RLoader to verify the expected bytes at its target before installation. Other hook addresses and character-list layouts remain build-specific. Sorting, selection synchronization, arrow controls, immediate panel close on entering gameplay, Custom-mode Ctrl+W/Ctrl+S ordering, and preview/frame synchronization after deleting a character were validated in-game in earlier versions. Version 0.9.29 hides the sort panel during Info/Options/Cinematics and Multiplayer overlays, then restores it when returning to character select; this was validated in-game.
