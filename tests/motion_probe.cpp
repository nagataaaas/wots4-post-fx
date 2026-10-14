#include "motion_blur.h"
#include "supersampling.h"
#include "test_support.h"
#include <limits>

namespace {
post_fx::Matrix identity() {
    post_fx::Matrix matrix;
    for (unsigned index = 0; index < 4; ++index) matrix.elements[index][index] = 1;
    return matrix;
}
post_fx::CameraSample sample() {
    post_fx::CameraSample camera;
    camera.view = identity();
    camera.projection.elements[0][0] = 1.3f;
    camera.projection.elements[1][1] = 1.7f;
    camera.projection.elements[2][0] = 0.12f;
    camera.projection.elements[2][1] = -0.07f;
    camera.projection.elements[2][2] = 100.0f / 99.5f;
    camera.projection.elements[2][3] = 1;
    camera.projection.elements[3][2] = -50.0f / 99.5f;
    camera.valid = true;
    camera.camera = 1; camera.scene = 2; camera.depth = 3;
    return camera;
}
}
int main() {
    auto camera = sample();
    post_fx::Matrix inverse;
    test::check("Asymmetric projection can be inverted", post_fx::invert(camera.projection, inverse));
    test::check("Projection times inverse is identity", post_fx::matricesMatch(post_fx::multiply(camera.projection, inverse), identity()));
    post_fx::Matrix singular;
    test::check("Singular matrices are rejected", !post_fx::invert(singular, inverse));
    singular.elements[0][0] = std::numeric_limits<float>::infinity();
    test::check("Non-finite matrices are rejected", !post_fx::invert(singular, inverse));
    post_fx::MotionHistory history;
    test::check("First draw has no exposure", !history.advance(camera, 1, 0, 1).valid);
    auto exposure = history.advance(camera, 1.033, 0, 1);
    test::check("Second stable draw retains the previous matrix", exposure.valid &&
        post_fx::matricesMatch(exposure.previous, post_fx::multiply(camera.view, camera.projection)));
    test::check("Exposure uses elapsed draw time", std::abs(exposure.scale - 0.016f / 0.033f) < 0.0001f);
    exposure = history.advance(camera, 1.038, 0, 1);
    test::check("Exposure is capped at one draw interval", exposure.valid && exposure.scale == 1);
    test::check("Setting changes discard history", !history.advance(camera, 1.071, 1, 1).valid);
    camera.camera = 5;
    test::check("Camera changes discard history", !history.advance(camera, 1.104, 1, 1).valid);
    camera.scene = 6;
    test::check("Scene changes discard history", !history.advance(camera, 1.137, 1, 1).valid);
    camera.depth = 7;
    test::check("Depth resource changes discard history", !history.advance(camera, 1.170, 1, 1).valid);
    test::check("Long gaps discard history", !history.advance(camera, 2, 1, 1).valid);
    test::check("Repeated timestamps discard history", !history.advance(camera, 2, 1, 1).valid);
    camera.projection.elements[0][0] = 2;
    test::check("Projection changes discard history", !history.advance(camera, 2.033, 1, 1).valid);
    camera.position[0] = 100;
    test::check("Camera teleports discard history", !history.advance(camera, 2.066, 1, 1).valid);
    camera.view.elements[2][2] = -1;
    test::check("Abrupt camera rotation discards history", !history.advance(camera, 2.099, 1, 1).valid);
    camera.valid = false;
    test::check("Missing camera data discards history", !history.advance(camera, 2.132, 1, 1).valid);
    camera = sample();
    history.clear();
    test::check("Reset starts a fresh history", !history.advance(camera, 3, 0, 1).valid);
    using post_fx::supersamplingOff;
    using post_fx::supersamplingOneAndHalf;
    using post_fx::supersamplingDouble;
    auto size = post_fx::supersampledSize(1920, 1080, supersamplingDouble, 8192, 8192);
    test::check("SSAA 2x doubles both scene axes and keeps the native size", size.width == 3840 && size.height == 2160 &&
        size.nativeWidth == 1920 && size.nativeHeight == 1080 && size.mode == supersamplingDouble);
    size = post_fx::supersampledSize(3840, 2160, supersamplingDouble, 8192, 8192);
    test::check("SSAA permits a 4K display when device limits allow", size.width == 7680 && size.height == 4320 && size.mode == supersamplingDouble);
    size = post_fx::supersampledSize(3840, 2160, supersamplingDouble, 4096, 4096);
    test::check("SSAA exceeding device dimensions falls back", size.width == 3840 && size.mode == supersamplingOff);
    size = post_fx::supersampledSize(5000, 4000, supersamplingDouble, 16384, 16384);
    test::check("SSAA exceeding the scene pixel budget falls back", size.width == 5000 && size.mode == supersamplingOff);
    size = post_fx::supersampledSize(UINT32_MAX, UINT32_MAX, supersamplingDouble, UINT32_MAX, UINT32_MAX);
    test::check("SSAA dimensions cannot overflow", size.width == UINT32_MAX && size.mode == supersamplingOff);
    size = post_fx::supersampledSize(0, 1080, supersamplingDouble, 8192, 8192);
    test::check("SSAA rejects empty targets", size.width == 0 && size.mode == supersamplingOff);
    size = post_fx::supersampledSize(1920, 1080, static_cast<post_fx::SupersamplingMode>(3), 8192, 8192);
    test::check("Unsupported modes preserve native resolution", size.width == 1920 && size.mode == supersamplingOff);
    size = post_fx::supersampledSize(1920, 1080, supersamplingOneAndHalf, 8192, 8192);
    test::check("SSAA 1.5x enlarges both axes by half", size.width == 2880 && size.height == 1620 &&
        size.nativeWidth == 1920 && size.nativeHeight == 1080 && size.mode == supersamplingOneAndHalf);
    size = post_fx::supersampledSize(1365, 767, supersamplingOneAndHalf, 8192, 8192);
    test::check("SSAA 1.5x rounds odd lengths up", size.width == 2048 && size.height == 1151);
    size = post_fx::supersampledSize(3840, 2160, supersamplingOneAndHalf, 4096, 4096);
    test::check("SSAA 1.5x of 4K exceeding device dimensions falls back", size.width == 3840 && size.mode == supersamplingOff);
    size = post_fx::supersampledSize(3840, 2160, supersamplingOneAndHalf, 8192, 8192);
    test::check("SSAA 1.5x of 4K fits the scene pixel budget", size.width == 5760 && size.height == 3240);
    test::check("Display-pixel widths scale with the mode", post_fx::sceneScale(supersamplingOff) == 1 &&
        post_fx::sceneScale(supersamplingOneAndHalf) == 1.5f && post_fx::sceneScale(supersamplingDouble) == 2);
    return test::finish();
}
