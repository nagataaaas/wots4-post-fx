#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace post_fx {
struct Matrix { float elements[4][4]{}; };
inline Matrix multiply(const Matrix& left, const Matrix& right) {
    Matrix result;
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned column = 0; column < 4; ++column)
            for (unsigned index = 0; index < 4; ++index)
                result.elements[row][column] += left.elements[row][index] * right.elements[index][column];
    return result;
}
inline bool finite(const Matrix& matrix) {
    for (const auto& row : matrix.elements)
        for (float value : row) if (!std::isfinite(value)) return false;
    return true;
}
inline bool matricesMatch(const Matrix& left, const Matrix& right, float tolerance = 0.0001f) {
    if (!finite(left) || !finite(right)) return false;
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned column = 0; column < 4; ++column)
            if (std::abs(left.elements[row][column] - right.elements[row][column]) > tolerance) return false;
    return true;
}
inline bool invert(const Matrix& matrix, Matrix& result) {
    if (!finite(matrix)) return false;
    double augmented[4][8]{};
    for (unsigned row = 0; row < 4; ++row) {
        for (unsigned column = 0; column < 4; ++column) augmented[row][column] = matrix.elements[row][column];
        augmented[row][row + 4] = 1;
    }
    for (unsigned column = 0; column < 4; ++column) {
        unsigned pivot = column;
        for (unsigned row = column + 1; row < 4; ++row)
            if (std::abs(augmented[row][column]) > std::abs(augmented[pivot][column])) pivot = row;
        if (std::abs(augmented[pivot][column]) < 1e-12) return false;
        for (unsigned index = 0; index < 8; ++index) std::swap(augmented[column][index], augmented[pivot][index]);
        const double divisor = augmented[column][column];
        for (double& value : augmented[column]) value /= divisor;
        for (unsigned row = 0; row < 4; ++row) {
            if (row == column) continue;
            const double factor = augmented[row][column];
            for (unsigned index = 0; index < 8; ++index) augmented[row][index] -= factor * augmented[column][index];
        }
    }
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned column = 0; column < 4; ++column)
            result.elements[row][column] = static_cast<float>(augmented[row][column + 4]);
    return finite(result);
}

struct CameraSample {
    Matrix view, projection;
    uintptr_t camera = 0, scene = 0, depth = 0;
    float position[3]{};
    bool valid = false;
};
struct Exposure {
    Matrix inverseCurrent, previous;
    float scale = 0;
    bool valid = false;
};

class MotionHistory {
public:
    void clear() { valid = false; }
    Exposure advance(const CameraSample& current, double seconds, uint32_t revision, uint32_t strength) {
        Exposure exposure;
        const Matrix combined = multiply(current.view, current.projection);
        if (!current.valid || !std::isfinite(seconds) || !invert(combined, exposure.inverseCurrent)) {
            clear();
            return exposure;
        }
        const double interval = seconds - previousSeconds;
        float distanceSquared = 0;
        for (unsigned axis = 0; axis < 3; ++axis) {
            const float distance = current.position[axis] - previous.position[axis];
            distanceSquared += distance * distance;
        }
        float directionDot = 0;
        for (unsigned axis = 0; axis < 3; ++axis)
            directionDot += current.view.elements[axis][2] * previous.view.elements[axis][2];
        // Discontinuities are camera cuts, not long exposures. Limits need in-game tuning.
        constexpr double minimumDrawInterval = 0.0001, maximumDrawInterval = 0.15;
        constexpr float maximumCameraDistanceSquared = 50.0f * 50.0f, minimumDirectionDot = 0.75f;
        exposure.valid = valid && revision == previousRevision && interval > minimumDrawInterval && interval < maximumDrawInterval &&
            current.camera == previous.camera && current.scene == previous.scene && current.depth == previous.depth &&
            matricesMatch(current.projection, previous.projection) && distanceSquared < maximumCameraDistanceSquared && directionDot > minimumDirectionDot;
        constexpr float exposureSeconds[] = {0.008f, 0.016f, 0.024f};
        if (exposure.valid) {
            exposure.previous = multiply(previous.view, previous.projection);
            exposure.scale = std::min(1.0f, exposureSeconds[std::min(strength, 2u)] / static_cast<float>(interval));
        }
        previous = current;
        previousSeconds = seconds;
        previousRevision = revision;
        valid = true;
        return exposure;
    }
private:
    CameraSample previous;
    double previousSeconds = 0;
    uint32_t previousRevision = 0;
    bool valid = false;
};
}
