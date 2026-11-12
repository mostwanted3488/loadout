# LOADOUT

GTA-style plugin wheel + quick search for FL Studio (VST3 / AU), with a liquid-glass look.
By [@buyorbealoser](https://github.com/mostwanted3488).

![wheel](https://img.shields.io/badge/formats-VST3%20%7C%20AU-white)

## Download
Grab the latest build from **Releases** (built automatically by GitHub Actions on every push):
- `LOADOUT-mac.zip` — VST3 + AU, Apple Silicon & Intel
- `LOADOUT-windows.zip` — VST3, 64-bit

Install steps are in `INSTALL.txt`.

## How it works
- LOADOUT is a tiny plugin host: the plugin you pick from the wheel runs *inside* LOADOUT.
- Opening the wheel captures the screen behind it, then a fullscreen window draws that capture
  with the glass wheel on top (OpenGL shader: blur, edge refraction, chromatic aberration,
  mouse-following highlight). On macOS this needs Screen Recording permission for the DAW.
- 10 slots (1–0): folders auto-filled from your scanned plugins (EQ, Dynamics, Reverb & Delay,
  Instruments, Distortion, Modulation, Utility, Other) + your pinned favourites.
- Recents, drag-to-pin, UI sounds, and "Send to: Pick tracks" to load a plugin on any track that
  has a LOADOUT (all instances in the project see each other).

## Build
```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```
JUCE 8 is fetched automatically.

## Known limits
- Instruments load fine, but FL only sends notes to mixer effects in some setups.
- The wheel lists VST3 plugins. Plugins without a moduleinfo.json are listed by file name until you load them once
  (then their real name, vendor and category are used).
