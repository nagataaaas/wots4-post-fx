# Validation

Status: implemented, tested outside the game and confirmed in the game. Rendering time and FPS have not been measured.

All tests run without starting the game. CI runs the synthetic tests. Tests labeled `needs_exe` need a local copy of the executable, and the test labeled `gpu` needs a Direct3D 9 hardware device (it skips when none is available). CI excludes both labels.

## Outside the game

- All seven embedded pixel shaders compile for Shader Model 3 using the Windows SDK compiler.
- Settings tests cover localized titles, six options, the toggle ABI, defaults, choice-name validation (case-insensitive names; numbers, unknown and truncated values fall back to defaults), history invalidation, deferred persistence, independent SMAA/SSAA/sharpening INI roundtrips, and the bundled config.ini (defaults without warnings; saving updates it in place and keeps its comments).
- Math tests cover matrix inversion, elapsed-time exposure, exposure bounds and history invalidation on camera, scene, projection, resource and timing changes.
- SSAA sizing tests cover doubled axes, supported 4K display dimensions, device limits, pixel budgets, overflow, empty sizes and unsupported sample factors.
- Fake D3D9 COM tests cover pass order, input/output separation, half-texel vertices, complete restoration of touched states, preservation of the depth-mask hook's tracked state, resource reuse, draw/capture/creation failures, device loss without a failure report, Reset release and resolution changes. These tests use no GPU.
- Fake-device checks record each draw's shader and depth sampler. Anti-aliasing runs first, sharpening next and motion blur last. SMAA edge detection reads point-sampled depth when a matching depth image exists and runs without it otherwise. The blur after SMAA reads depth by point again, not with the area lookup's linear filter.
- Fake-device checks also cover stale-edge clears, all three sampler slots, SMAA weight-pass failures, SSAA downsample failures, and complete release of lookup/resolve resources. A failed SMAA setup is reported once, stops only SMAA while blur, FXAA and the SSAA resolve continue, and is retried after Reset. Linear-light blending is used only when the device reports sRGB read/write support for the scene format.
- A standalone GPU test renders synthetic diagonal/low-contrast/very-low-contrast edges, a one-pixel vertical line, a checkerboard and flat color through the actual embedded FXAA shader. It checks interior pixel changes, stronger thin-line filtering with adjacent-pixel coverage, unchanged flat regions and alpha preservation. It also renders rotating-camera blur on near/far planes to check proximity attenuation. Only the test's own textures are read back; the game is not run and the hidden window is never presented.
- SMAA GPU checks cover geometric edges, near-isoluminant color edges, flat regions after a previous edge frame, and varying per-pixel alpha. Standard 8-bit display targets use sRGB decode/encode for neighborhood blending; edge and lookup passes do not. Float targets retain their linear values.
- SMAA uses the Ultra preset with depth predication. On the GPU, a silhouette whose colors differ by 8/255 stays unchanged without depth and is smoothed along a depth step. A faint texture (4/255) away from the step stays unchanged. The depth comparison uses relative distance (about 5%), derived from the game's depth encoding.
- Sharpening GPU checks feed a soft ramp through the Low, Normal and High levels. Each level makes the ramp steeper, and higher levels change more pixels. Flat regions and per-pixel alpha stay unchanged.
- SSAA GPU checks verify the four-sample reduction in linear light, resolved-pixel replication, and coexistence with SMAA. A real polygon is rasterized separately at native and doubled resolution; only the doubled rendering produces partially covered pixels after the resolve. This checks actual new spatial samples, rather than enlargement of a finished image.
- Tests labeled `needs_exe` map a copied executable, replace the selected function bodies with inert returns and load the Mod. They cover the export, API validation, hook claims, calling conventions, unsupported bytes, conflicting claims, failed registration, rollback, and the original Steam image's header size. No game entry point, TLS or original frame/camera/D3D body is executed.
- The scene-creator tests verify scaled arguments, native fallback, restart-only selection, layout rejection and rollback when the additional hook conflicts.

The SDK integration exposes one page in both options menus.

FXAA uses a relative edge threshold of 1/16, a minimum threshold of 1/128, a subpixel trim of 1/8 and a maximum subpixel blend of 0.75. Endpoint search uses nine distances through 16 pixels. Fine detail can become softer. Rendering cost has not been measured.

## In the game

The options page, anti-aliasing (FXAA, SMAA), sharpening, SSAA and camera motion blur have been confirmed in the game. Rendering time, FPS and GPU memory use have not been measured. Motion blur intentionally skips missing, ambiguous or unsupported camera/depth inputs, partial camera ports, and frames drawn while the game window is minimized or in the background; anti-aliasing and the SSAA resolve continue in those frames.

The passes read back the device state they change so they can restore it. If the Direct3D 9 runtime refuses those reads, every pass is skipped and one error is logged.

SSAA changes are saved for the next game start. The scene creator doubles raw color, encoded depth and both postprocess targets; dependent depth/stencil buffers and existing effect targets follow their actual source sizes. Display/UI targets remain native-sized. A shader resolve averages each 2x2 footprint in linear light when the device supports sRGB conversion, then replicates the resolved pixel into the existing scene texture for the game's final scene copy. This adds a reduction and a full-resolution write pass. Device dimension limits and a 64 Mi-pixel budget reject oversized requests; passing these limits does not guarantee enough memory. GPU memory consumption and FPS remain unmeasured.

The exposure values are 8/16/24 ms, capped to one draw interval. Maximum distant blur width is 32 pixels. Within 8 camera-distance units the exposure multiplier is 0.1 and width limit is 3 pixels; both smoothly reach full strength by 50 units. Depth boundaries use camera-space depth differences, so adjacent points on a flat surface are not mistaken for separate depths. Actual CPU/GPU time and FPS have not been measured.

Shader and scratch resources are created only on first enabled use or device/target changes. Ordinary draws do not allocate CPU heap memory, read images back to the CPU, write files or format log strings. Diagnostics are rate-limited and reported from the update callback.

Game files and saves are not patched. Tests only write their own build-directory fixtures and mapped executable memory. Game testing requires user authorization.
