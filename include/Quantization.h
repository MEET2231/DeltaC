#pragma once

#include <cstdint>
#include <cmath>
#include <algorithm>
#include <cassert>

namespace DeltaCompression {

// Quantizes a float value in [minVal, maxVal] to a 16-bit unsigned integer [0, 65535].
// Maximum roundoff error is half a step: (maxVal - minVal) / (2 * 65535).
[[nodiscard]] inline uint16_t QuantizeFloat(float val, float minVal, float maxVal) noexcept {
    assert(maxVal > minVal);

    // 1. Clamp input to valid bounds
    const float clampedVal = std::clamp(val, minVal, maxVal);

    // 2. Normalize to unit interval [0.0f, 1.0f]
    const float normalized = (clampedVal - minVal) / (maxVal - minVal);

    // 3. Scale to 16-bit unsigned integer range [0, 65535] with rounding
    constexpr float maxInt16 = 65535.0f;
    return static_cast<uint16_t>(std::round(normalized * maxInt16));
}

// Dequantizes a 16-bit unsigned integer [0, 65535] back to floating-point [minVal, maxVal].
[[nodiscard]] inline float DequantizeFloat(uint16_t val, float minVal, float maxVal) noexcept {
    assert(maxVal > minVal);

    // 1. Normalize integer to unit interval [0.0f, 1.0f]
    constexpr float maxInt16 = 65535.0f;
    const float normalized = static_cast<float>(val) / maxInt16;

    // 2. Scale and translate back to world coordinate range
    return minVal + normalized * (maxVal - minVal);
}

// Generalized quantization mapping float [minVal, maxVal] to [1, 32] bits.
[[nodiscard]] inline uint32_t QuantizeFloatBits(float val, float minVal, float maxVal, int bitCount) noexcept {
    assert(maxVal > minVal);
    assert(bitCount >= 1 && bitCount <= 32);

    const float clampedVal = std::clamp(val, minVal, maxVal);
    const float normalized = (clampedVal - minVal) / (maxVal - minVal);

    const uint32_t maxInt = (bitCount == 32)
        ? 0xFFFFFFFFU
        : ((1U << bitCount) - 1U);

    return static_cast<uint32_t>(std::round(normalized * static_cast<float>(maxInt)));
}

// Generalized dequantization mapping [1, 32] bit integer back to float [minVal, maxVal].
[[nodiscard]] inline float DequantizeFloatBits(uint32_t val, float minVal, float maxVal, int bitCount) noexcept {
    assert(maxVal > minVal);
    assert(bitCount >= 1 && bitCount <= 32);

    const uint32_t maxInt = (bitCount == 32)
        ? 0xFFFFFFFFU
        : ((1U << bitCount) - 1U);

    const float normalized = static_cast<float>(val) / static_cast<float>(maxInt);
    return minVal + normalized * (maxVal - minVal);
}

} // namespace DeltaCompression
