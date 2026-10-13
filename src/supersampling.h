#pragma once
#include <cstdint>

namespace post_fx {
struct SceneSize { uint32_t width, height; unsigned factor; };
inline SceneSize supersampledSize(uint32_t width, uint32_t height, unsigned factor,
    uint32_t maximumWidth, uint32_t maximumHeight) {
    // Bound the main scene allocation in a 32-bit game; effect buffers add more memory.
    constexpr uint64_t maximumScenePixels = 64ull * 1024 * 1024;
    if (factor != 2 || !width || !height) return {width, height, 1};
    const uint64_t enlargedWidth = static_cast<uint64_t>(width) * factor;
    const uint64_t enlargedHeight = static_cast<uint64_t>(height) * factor;
    if (enlargedWidth > maximumWidth || enlargedHeight > maximumHeight ||
        enlargedWidth * enlargedHeight > maximumScenePixels) return {width, height, 1};
    return {static_cast<uint32_t>(enlargedWidth), static_cast<uint32_t>(enlargedHeight), factor};
}
}
