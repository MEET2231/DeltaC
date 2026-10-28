#pragma once

#include "Quantization.h"
#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <cassert>

namespace DeltaCompression {

// High-performance BitReader for unpacking bit streams serialized by BitWriter.
// Matches LSB-first ordering across arbitrary byte boundaries with overflow detection.
class BitReader {
public:
    BitReader() noexcept : m_buffer(nullptr), m_bitCapacity(0), m_bitIndex(0), m_overflow(false) {}

    BitReader(const uint8_t* buffer, size_t byteCount) noexcept {
        Init(buffer, byteCount * 8);
    }

    BitReader(const uint8_t* buffer, size_t totalBits, bool isBits) noexcept {
        (void)isBits;
        Init(buffer, totalBits);
    }

    void Init(const uint8_t* buffer, size_t totalBits) noexcept {
        m_buffer = buffer;
        m_bitCapacity = totalBits;
        m_bitIndex = 0;
        m_overflow = false;
    }

    void Reset() noexcept {
        m_bitIndex = 0;
        m_overflow = false;
    }

    // Read 1-bit boolean flag
    [[nodiscard]] bool ReadBit() noexcept {
        return ReadBits(1) != 0;
    }

    // Read [1, 32] bits tightly across byte boundaries (LSB first)
    [[nodiscard]] uint32_t ReadBits(int bitCount) noexcept {
        if (bitCount <= 0) {
            return 0;
        }

        assert(bitCount <= 32);

        // Explicit buffer overflow detection
        if (m_bitIndex + static_cast<size_t>(bitCount) > m_bitCapacity) {
            m_overflow = true;
            return 0;
        }

        uint32_t result = 0;
        int bitsReadSoFar = 0;
        int remainingBits = bitCount;

        while (remainingBits > 0) {
            const size_t byteIndex = m_bitIndex >> 3;       // m_bitIndex / 8
            const size_t bitOffset = m_bitIndex & 0x7;      // m_bitIndex % 8
            const int bitsAvailableInByte = static_cast<int>(8 - bitOffset);
            const int bitsToRead = std::min(remainingBits, bitsAvailableInByte);

            // Extract chunk from current byte
            const uint8_t chunkMask = static_cast<uint8_t>((1U << bitsToRead) - 1U);
            const uint8_t chunk = static_cast<uint8_t>((m_buffer[byteIndex] >> bitOffset) & chunkMask);

            // Place chunk into accumulator at current bit position
            result |= (static_cast<uint32_t>(chunk) << bitsReadSoFar);

            m_bitIndex += bitsToRead;
            bitsReadSoFar += bitsToRead;
            remainingBits -= bitsToRead;
        }

        return result;
    }

    // Read and dequantize float from [minVal, maxVal] stored across [1, 32] bits (defaults to 16 bits)
    [[nodiscard]] float ReadQuantizedFloat(float minVal, float maxVal, int bitCount = 16) noexcept {
        const uint32_t quantized = ReadBits(bitCount);
        return DequantizeFloatBits(quantized, minVal, maxVal, bitCount);
    }

    // Align read head to the next whole byte boundary
    void AlignToByte() noexcept {
        const size_t remainder = m_bitIndex & 0x7;
        if (remainder != 0) {
            m_bitIndex += (8 - remainder);
        }
    }

    [[nodiscard]] const uint8_t* GetData() const noexcept { return m_buffer; }
    [[nodiscard]] size_t GetBitsRead() const noexcept { return m_bitIndex; }
    [[nodiscard]] size_t GetBitsRemaining() const noexcept {
        return (m_bitIndex <= m_bitCapacity) ? (m_bitCapacity - m_bitIndex) : 0;
    }
    [[nodiscard]] size_t GetBitCapacity() const noexcept { return m_bitCapacity; }
    [[nodiscard]] bool HasOverflowed() const noexcept { return m_overflow; }

private:
    const uint8_t* m_buffer{nullptr};
    size_t         m_bitCapacity{0};
    size_t         m_bitIndex{0};
    bool           m_overflow{false};
};

} // namespace DeltaCompression
