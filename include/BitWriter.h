#pragma once

#include "Quantization.h"
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <algorithm>
#include <cassert>

namespace DeltaCompression {

// High-performance BitWriter for bit-packed serialization over UDP.
// Packs arbitrary bit lengths across byte boundaries using LSB-first ordering.
// Operates on caller-supplied memory with zero dynamic allocations.
class BitWriter {
public:
    BitWriter() noexcept : m_buffer(nullptr), m_byteCapacity(0), m_bitCapacity(0), m_bitIndex(0), m_overflow(false) {}

    BitWriter(uint8_t* buffer, size_t byteCapacity) noexcept {
        Init(buffer, byteCapacity);
    }

    void Init(uint8_t* buffer, size_t byteCapacity) noexcept {
        m_buffer = buffer;
        m_byteCapacity = byteCapacity;
        m_bitCapacity = byteCapacity * 8;
        m_bitIndex = 0;
        m_overflow = false;

        if (m_buffer && m_byteCapacity > 0) {
            std::memset(m_buffer, 0, m_byteCapacity);
        }
    }

    // Reset bit write head to reuse existing buffer without reallocation
    void Reset() noexcept {
        m_bitIndex = 0;
        m_overflow = false;
        if (m_buffer && m_byteCapacity > 0) {
            std::memset(m_buffer, 0, m_byteCapacity);
        }
    }

    // Pack 1-bit boolean flag
    void WriteBit(bool bit) noexcept {
        WriteBits(bit ? 1U : 0U, 1);
    }

    // Write [1, 32] bits tightly across byte boundaries (LSB first)
    void WriteBits(uint32_t data, int bitCount) noexcept {
        if (bitCount <= 0) {
            return;
        }

        assert(bitCount <= 32);

        // Explicit buffer overflow detection
        if (m_bitIndex + static_cast<size_t>(bitCount) > m_bitCapacity) {
            m_overflow = true;
            return;
        }

        // Mask off unused high bits in source value to prevent bit pollution
        const uint32_t sanitizedData = (bitCount == 32)
            ? data
            : (data & ((1U << bitCount) - 1U));

        uint32_t remainingData = sanitizedData;
        int remainingBits = bitCount;

        while (remainingBits > 0) {
            const size_t byteIndex = m_bitIndex >> 3;       // m_bitIndex / 8
            const size_t bitOffset = m_bitIndex & 0x7;      // m_bitIndex % 8
            const int bitsFreeInByte = static_cast<int>(8 - bitOffset);
            const int bitsToWrite = std::min(remainingBits, bitsFreeInByte);

            // Bitmask for the chunk to write into the current byte
            const uint8_t chunkMask = static_cast<uint8_t>((1U << bitsToWrite) - 1U);
            const uint8_t chunk = static_cast<uint8_t>(remainingData & chunkMask);

            // Shift chunk into position and OR with existing byte
            m_buffer[byteIndex] |= static_cast<uint8_t>(chunk << bitOffset);

            remainingData >>= bitsToWrite;
            m_bitIndex += bitsToWrite;
            remainingBits -= bitsToWrite;
        }
    }

    // Quantize float in [minVal, maxVal] to [1, 32] bits (defaults to 16 bits)
    void WriteQuantizedFloat(float val, float minVal, float maxVal, int bitCount = 16) noexcept {
        const uint32_t quantized = QuantizeFloatBits(val, minVal, maxVal, bitCount);
        WriteBits(quantized, bitCount);
    }

    // Advance write head to next whole byte boundary (zero-padded)
    void AlignToByte() noexcept {
        const size_t remainder = m_bitIndex & 0x7;
        if (remainder != 0) {
            WriteBits(0, static_cast<int>(8 - remainder));
        }
    }

    [[nodiscard]] const uint8_t* GetData() const noexcept { return m_buffer; }
    [[nodiscard]] uint8_t* GetData() noexcept { return m_buffer; }
    [[nodiscard]] size_t GetBitsWritten() const noexcept { return m_bitIndex; }
    [[nodiscard]] size_t GetBytesWritten() const noexcept { return (m_bitIndex + 7) >> 3; }
    [[nodiscard]] size_t GetBitCapacity() const noexcept { return m_bitCapacity; }
    [[nodiscard]] size_t GetByteCapacity() const noexcept { return m_byteCapacity; }
    [[nodiscard]] bool HasOverflowed() const noexcept { return m_overflow; }

private:
    uint8_t* m_buffer{nullptr};
    size_t   m_byteCapacity{0};
    size_t   m_bitCapacity{0};
    size_t   m_bitIndex{0};
    bool     m_overflow{false};
};

} // namespace DeltaCompression
