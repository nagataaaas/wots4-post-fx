# Post FX

[日本語](README.md)

A Mod for the Steam version of *Way of the Samurai 4* that adds anti-aliasing and camera motion blur.

This prototype has been tested outside the game. Image quality, the options tab, UI exclusion across screens, device recovery and compatibility with other Mods still require in-game verification.

## Features

- FXAA reduces jagged edges. Fine patterns can become softer.
- SMAA 1x detects edge patterns and uses the official implementation's High preset.
- SSAA renders the 3D scene at twice the resolution on each axis, then reduces it by averaging four pixels. It can be combined with FXAA or SMAA. UI resolution stays unchanged.
- Motion blur follows camera movement. It does not blur independently moving characters or swords while the camera is stationary.
- Blur strength and maximum width are reduced near the camera. This distance-based adjustment applies to the player, nearby NPCs and foreground objects.
- Enable each effect independently. The effects apply to the 3D image, not to text or the UI.
- Draws without usable camera or depth data skip motion blur; anti-aliasing remains independent.
- While the game window is minimized or in the background, only motion blur pauses; anti-aliasing and SSAA continue.
- After scene changes, camera changes, abrupt view changes, device recovery and similar interruptions, motion blur pauses briefly and then resumes.

## Settings

The Mod adds a "Post FX" tab ("映像後処理" in Japanese) to the title-screen and in-game options. Changes other than SSAA apply to the next draw. Save the options, exit the game and restart to apply SSAA changes. While SSAA differs from the setting in use, a notice below it says a restart is needed.

| Setting | Choices | Default |
| --- | --- | --- |
| Anti-Aliasing | Off / FXAA / SMAA | Off |
| Motion Blur | Off / On | Off |
| Blur Strength | Low / Normal / High | Normal |
| Blur Quality | Low / Normal / High | Normal |
| SSAA | Off / 2x per axis (4 samples) | Off |

Strength changes exposure, and quality changes the sample count (4 / 8 / 12). Higher quality increases GPU work. Actual rendering time and FPS have not been measured.

SSAA quadruples the rendered pixel count and significantly increases GPU work and memory usage. If the enlarged scene exceeds device texture dimensions or the 64 Mi-pixel scene limit, the scene is drawn at native resolution. Even within these limits, the GPU may not have enough memory. Compare SMAA alone first, then try SSAA if performance permits.

Strength and quality are retained while blur is off. The values live in `mods/post_fx/config.ini`, which the game writes when it saves its options. Exit the game before editing the file directly. Values are not case-sensitive. Missing keys and invalid values use the defaults.

| Section | Key | Default | Description |
| --- | --- | --- | --- |
| `antialiasing` | `method` | `off` | Anti-aliasing: `off` / `fxaa` / `smaa` |
| `antialiasing` | `supersampling` | `off` | SSAA: `off` / `2x` (twice per axis, 4 samples). Applied at the next game start |
| `motion_blur` | `enabled` | `0` | Motion blur: `1` on, `0` off |
| `motion_blur` | `strength` | `normal` | Blur strength: `low` / `normal` / `high` |
| `motion_blur` | `quality` | `normal` | Blur quality: `low` / `normal` / `high` |

To keep your current settings when updating, replace only `mod.dll` and do not overwrite `mods/post_fx/config.ini`.

## Building and placement

Requires the [wots4-mods (ModLoader)](https://github.com/nagataaaas/wots4-mods) SDK, MSVC, and the Windows SDK including `fxc.exe`. Builds target x86 and C++17. Shaders are compiled and embedded in the DLL at build time; no shader compiler or additional shader files are required at runtime.

```powershell
cmake -S . -B build -A Win32 -DWOTS4_SDK_DIR="C:/path/to/wots4-mods"
cmake --build build --config Release
ctest --test-dir build -C Release -LE needs_exe --output-on-failure
```

Output: `build/game/mods/post_fx/mod.dll`. For an in-game trial, place it at `mods/post_fx/mod.dll` in a game installation with the ModLoader installed.

See [docs/VALIDATION.md](docs/VALIDATION.md) for test coverage and remaining checks.

## License

MinHook's license is in [docs/licenses/minhook.txt](docs/licenses/minhook.txt), and SMAA's license is in [docs/licenses/smaa.txt](docs/licenses/smaa.txt). The game's executable and existing shader source files are not included in distributions.
