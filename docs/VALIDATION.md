# Validation

Status: implemented prototype; in-game verification pending.

Release build results: 277 checks passed, 0 failed across 12 CTest entries, including 74 standalone GPU checks.
The revised Mod compiles successfully as a standalone build. Initial integration was also built in the root project.
The 3 CI entries contain 118 checks; 8 entries are labeled `needs_exe`, and 1 is labeled `gpu`.
CI excludes both optional labels; the GPU test skips when no D3D9 HAL device is available.

## Outside the game

- All six embedded pixel shaders compile for Shader Model 3 using the Windows SDK compiler.
- Settings tests cover localized titles, five options, the toggle ABI, defaults, range validation, history invalidation, deferred persistence and independent SMAA/SSAA INI roundtrips.
- Math tests cover matrix inversion, elapsed-time exposure, exposure bounds and history invalidation on camera, scene, projection, resource and timing changes.
- SSAA sizing tests cover doubled axes, supported 4K display dimensions, device limits, pixel budgets, overflow, empty sizes and unsupported sample factors.
- Fake D3D9 COM tests cover pass order, input/output separation, half-texel vertices, complete restoration of touched states, preservation of the depth-mask hook's tracked state, resource reuse, draw/capture/creation failures, device loss, Reset release and resolution changes. These tests use no GPU.
- A standalone GPU test renders synthetic diagonal/low-contrast/very-low-contrast edges, a one-pixel vertical line, a checkerboard and flat color through the actual embedded FXAA shader. It checks interior pixel changes, stronger thin-line filtering with adjacent-pixel coverage, unchanged flat regions and alpha preservation. It also renders rotating-camera blur on near/far planes to check proximity attenuation. Only the test's own textures are read back; the game is not run and the hidden window is never presented.
- SMAA GPU checks cover geometric edges, near-isoluminant color edges, flat regions after a previous edge frame, and varying per-pixel alpha. Standard 8-bit display targets use sRGB decode/encode for neighborhood blending; edge and lookup passes do not. Float targets retain their linear values.
- SSAA GPU checks verify exact four-sample reduction, resolved-pixel replication, and coexistence with SMAA. A real polygon is rasterized separately at 64x64 and 128x128; the high-resolution resolve produces 107 pixels with partial coverage, compared with zero in the native rasterization. This checks actual new spatial samples, rather than enlargement of a finished image.
- Fake-device checks additionally cover stale-edge clears, all three sampler slots, SMAA weight-pass failures, SSAA downsample failures, resource reuse and complete release of lookup/resolve resources.
- Tests labeled `needs_exe` map a copied executable, replace the selected function bodies with inert returns and load the Mod. They cover the export, API validation, hook claims, calling conventions, unsupported bytes, conflicting claims, failed registration, rollback, and the original Steam image's header size. No game entry point, TLS or original frame/camera/D3D body is executed.
- The scene-creator tests verify scaled arguments, native fallback, restart-only selection, layout rejection and rollback when the additional hook conflicts.

The SDK integration exposes one page in both options menus. Its actual appearance remains unverified in the game.

The stronger FXAA tuning uses a relative edge threshold of 1/16, a minimum threshold of 1/128, a subpixel trim of 1/8 and a maximum subpixel blend of 0.75. Endpoint search uses nine distances through 16 pixels. Fine detail can become softer; in-game quality and rendering cost still need verification.

## In-game checks still required

Verify Off/FXAA/SMAA, SSAA alone and combinations with blur on the same scene, all four DoF/HDR combinations, UI text, fine edges, transparent effects, sky, title/loading/video/event/night-crawling screens, camera cuts, pause/resume, focus recovery, resolution changes and existing rendering Mods. Motion blur intentionally skips missing, ambiguous or unsupported camera/depth inputs and partial camera ports.

SSAA changes are saved for the next game start. The scene creator doubles raw color, encoded depth and both postprocess targets; dependent depth/stencil buffers and existing effect targets follow their actual source sizes. Display/UI targets remain native-sized. A shader resolve averages each 2x2 footprint, then replicates the resolved pixel into the existing scene texture for the game's final scene copy. This adds a reduction and a full-resolution write pass. Confirm actual game target sizes, downsample alignment, allocation success and existing HDR/DoF sampling offsets in-game. Device dimension limits and a 64 Mi-pixel budget reject oversized requests; passing these limits does not guarantee enough memory. GPU memory consumption and FPS remain unmeasured.

The exposure values are 8/16/24 ms, capped to one draw interval. Maximum distant blur width is 32 pixels. Within 8 camera-distance units the exposure multiplier is 0.1 and width limit is 3 pixels; both smoothly reach full strength by 50 units. Depth boundaries use camera-space depth differences, so adjacent points on a flat surface are not mistaken for separate depths. These thresholds need in-game visual tuning. Actual CPU/GPU time and FPS have not been measured; synthetic output has been verified but revised in-game quality is pending.

Shader and scratch resources are created only on first enabled use or device/target changes. Ordinary draws do not allocate CPU heap memory, read images back to the CPU, write files or format log strings. Diagnostics are rate-limited and reported from the update callback.

Game files and saves are not patched. Tests only write their own build-directory fixtures and mapped executable memory. Game testing requires user authorization.
