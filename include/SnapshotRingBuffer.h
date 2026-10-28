#pragma once

#include "NetworkStateTraits.h"
#include <array>
#include <cstdint>
#include <cstddef>
#include <optional>
#include <type_traits>

namespace DeltaCompression {

// RFC 1982 Serial Number Arithmetic for 16-bit sequence numbers.
// Handles modular wrap-around seamlessly over the range [0, 65535].
inline bool SequenceGreaterThan(uint16_t s1, uint16_t s2) noexcept {
    return static_cast<int16_t>(s1 - s2) > 0;
}

inline bool SequenceLessThan(uint16_t s1, uint16_t s2) noexcept {
    return static_cast<int16_t>(s1 - s2) < 0;
}

inline int16_t SequenceDifference(uint16_t s1, uint16_t s2) noexcept {
    return static_cast<int16_t>(s1 - s2);
}

// Fixed-capacity templated Ring Buffer for storing authoritative snapshots of any user-defined struct T.
// Guarantees zero runtime heap allocations, strict contiguous memory layout, and O(1) bitwise lookups.
template <typename T, size_t Capacity = 256>
class SnapshotRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two for fast bitwise indexing.");
    static_assert(Capacity <= 32768, "Capacity cannot exceed half of uint16_t range to avoid sequence ambiguity.");
    static_assert(std::is_default_constructible_v<T>, "T must be default-constructible.");
    static_assert(std::is_copy_constructible_v<T>, "T must be copy-constructible.");

public:
    struct Slot {
        uint16_t sequenceNumber{0};
        bool isValid{false};
        T state{};
    };

    SnapshotRingBuffer() noexcept {
        Reset();
    }

    // Reset buffer state
    void Reset() noexcept {
        for (auto& slot : m_slots) {
            slot.isValid = false;
            slot.sequenceNumber = 0;
            slot.state = T{};
        }
        m_latestSequence = 0;
        m_hasData = false;
    }

    // Store a snapshot by its authoritative 16-bit sequence number.
    // Overwrites old slots cleanly when the sequence number wraps around the circular buffer.
    void Insert(uint16_t sequence, const T& state) noexcept {
        const size_t index = GetIndex(sequence);
        m_slots[index].sequenceNumber = sequence;
        m_slots[index].isValid = true;
        m_slots[index].state = state;

        if (!m_hasData || SequenceGreaterThan(sequence, m_latestSequence)) {
            m_latestSequence = sequence;
            m_hasData = true;
        }
    }

    // Retrieve a state pointer for a specific sequence number.
    // Returns nullptr if the slot has been overwritten or was never populated.
    [[nodiscard]] const T* Get(uint16_t sequence) const noexcept {
        const size_t index = GetIndex(sequence);
        const Slot& slot = m_slots[index];

        // Ensure the slot is valid AND holds the exact sequence requested
        // (prevents stale reads if the ring buffer wrapped around and overwrote the slot)
        if (slot.isValid && slot.sequenceNumber == sequence) {
            return &slot.state;
        }
        return nullptr;
    }

    // Mutable access for in-place construction or reconstruction
    [[nodiscard]] T* GetMutable(uint16_t sequence) noexcept {
        const size_t index = GetIndex(sequence);
        Slot& slot = m_slots[index];

        if (slot.isValid && slot.sequenceNumber == sequence) {
            return &slot.state;
        }
        return nullptr;
    }

    // Check if a sequence number is currently retained in memory
    [[nodiscard]] bool Contains(uint16_t sequence) const noexcept {
        return Get(sequence) != nullptr;
    }

    // Returns the most recently inserted sequence number
    [[nodiscard]] std::optional<uint16_t> GetLatestSequence() const noexcept {
        if (!m_hasData) {
            return std::nullopt;
        }
        return m_latestSequence;
    }

    // Total capacity of the circular buffer
    [[nodiscard]] constexpr size_t GetCapacity() const noexcept {
        return Capacity;
    }

    // Bitmask for fast indexing: (Capacity - 1)
    [[nodiscard]] constexpr size_t GetIndexMask() const noexcept {
        return Capacity - 1;
    }

private:
    // Fast bitwise indexing: avoids expensive integer division (seq % Capacity)
    [[nodiscard]] constexpr size_t GetIndex(uint16_t sequence) const noexcept {
        return static_cast<size_t>(sequence & (Capacity - 1));
    }

    // Contiguous memory block: cache-line aligned
    alignas(64) std::array<Slot, Capacity> m_slots;
    uint16_t m_latestSequence{0};
    bool m_hasData{false};
};

} // namespace DeltaCompression
