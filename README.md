# SA.StoriesSprinting

A native GTA: San Andreas ASI that replaces H-G's "Stories Sprinting" CLEO
script. It lets you sprint with heavy weapons and use the one-handed jog run
animation, with a **configurable weapon list** (edit `SA.StoriesSprinting.ini`)
that supports **fastman92 add-on weapons** — matched by weapon type *or* model
ID. The "eASIer" memory patches are folded in, so no separate patcher plugin is
needed.

<p align="center">
  <img src="assets/preview1.gif" width="42%" />
  <img src="assets/preview2.gif" width="42%" />
</p>

Built with [plugin-sdk](https://github.com/DK22Pac/plugin-sdk). Targets
**GTA SA 1.0 US (HOODLUM)**.

## Install

Drop `SA.StoriesSprinting.asi` and `SA.StoriesSprinting.ini` into your game
(with an ASI loader). Keep the rest of the animation mod — the `.ifp`
animations and the weapon.dat anim-group edits — in place.

## Config

`[JogWeapons]` and `[FireExtWeapons]` in `SA.StoriesSprinting.ini` take
comma/space-separated weapon IDs (weapon type for vanilla, model ID for add-on
weapons). Reloads live while you play.

## Build

Needs the plugin-sdk with the `PLUGIN_SDK_DIR` environment variable set.
Build `SA.StoriesSprinting.vcxproj` (configuration **Release GTA-SA**, platform
**Win32**, toolset **v145**).
