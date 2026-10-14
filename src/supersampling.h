#pragma once
#include <cstdint>

namespace post_fx {
// SSAA choices, in the order of the options page.
enum SupersamplingMode : uint32_t { supersamplingOff, supersamplingOneAndHalf, supersamplingDouble };

// nativeWidth/Height are the display-sized scene the game asked for. With 2x every native pixel
// is an exact 2x2 block; with 1.5x native pixels do not line up with whole scene texels.
struct SceneSize {
    uint32_t width = 0, height = 0;
    uint32_t nativeWidth = 0, nativeHeight = 0;
    SupersamplingMode mode = supersamplingOff;
};
// Scene pixels per native pixel along one axis, for effects measured in display pixels.
inline float sceneScale(SupersamplingMode mode) {
    return mode == supersamplingDouble ? 2.0f : mode == supersamplingOneAndHalf ? 1.5f : 1.0f;
}
inline SceneSize nativeSize(uint32_t width, uint32_t height) { return {width, height, width, height, supersamplingOff}; }
inline SceneSize supersampledSize(uint32_t width, uint32_t height, SupersamplingMode mode,
    uint32_t maximumWidth, uint32_t maximumHeight) {
    // Bound the main scene allocation in a 32-bit game; effect buffers add more memory.
    constexpr uint64_t maximumScenePixels = 64ull * 1024 * 1024;
    const SceneSize native = nativeSize(width, height);
    if ((mode != supersamplingOneAndHalf && mode != supersamplingDouble) || !width || !height) return native;
    // 1.5x rounds odd lengths up; the 1.5x resolve weights each texel by its actual coverage.
    const auto enlarge = [mode](uint64_t length) { return mode == supersamplingDouble ? length * 2 : (length * 3 + 1) / 2; };
    const uint64_t enlargedWidth = enlarge(width), enlargedHeight = enlarge(height);
    if (enlargedWidth > maximumWidth || enlargedHeight > maximumHeight ||
        enlargedWidth * enlargedHeight > maximumScenePixels) return native;
    return {static_cast<uint32_t>(enlargedWidth), static_cast<uint32_t>(enlargedHeight), width, height, mode};
}
}
