#pragma once

#include "PacketHeader.h"
#include "BitReader.h"
#include "SnapshotRingBuffer.h"
#include "NetworkStateTraits.h"
#include <cstring>
#include <optional>

namespace DeltaCompression {

// Client-side replication manager for receiving, validating, and reconstructing
// delta-compressed snapshots over UDP into a local contiguous Ring Buffer.
template <typename T, size_t Capacity = 256>
class ClientReplicationManager {
    static_assert(is_delta_serializable_v<T>, "Type T must satisfy delta serialization requirements.");

public:
    ClientReplicationManager() noexcept : m_latestSequence(0), m_hasReceivedState(false), m_droppedPacketsCount(0) {}

    // Resets client state
    void Reset() noexcept {
        m_ringBuffer.Reset();
        m_latestSequence = 0;
        m_hasReceivedState = false;
        m_droppedPacketsCount = 0;
    }

    // Parses incoming datagram payload.
    // Rejects out-of-order packets, locates the historical baseline in the local ring buffer,
    // deserializes dirty fields, and inserts reconstructed state into the ring buffer.
    bool ProcessSnapshotPacket(const uint8_t* packetData,
                               size_t packetSize,
                               uint16_t& outSequence,
                               T& outReconstructedState) noexcept {
        if (!packetData || packetSize < sizeof(PacketHeader)) {
            return false;
        }

        PacketHeader header;
        std::memcpy(&header, packetData, sizeof(PacketHeader));

        if (header.packetType != PACKET_TYPE_DELTA) {
            return false;
        }

        const uint16_t incomingSeq = header.sequenceNumber;
        const uint16_t baselineSeq = header.ackNumber;

        // Step 1: Discard out-of-order or duplicate packets using RFC 1982
        if (m_hasReceivedState && !SequenceGreaterThan(incomingSeq, m_latestSequence)) {
            m_droppedPacketsCount++;
            return false;
        }

        // Step 2: Retrieve local Baseline snapshot from Ring Buffer
        T baselineState{};
        if (m_hasReceivedState) {
            const T* storedBaseline = m_ringBuffer.Get(baselineSeq);
            if (!storedBaseline) {
                // Baseline has expired or was never received; cannot reconstruct delta safely
                m_droppedPacketsCount++;
                return false;
            }
            baselineState = *storedBaseline;
        }

        // Step 3: Parse BitReader stream, read DirtyMask, and reconstruct fields
        BitReader reader(packetData + sizeof(PacketHeader), packetSize - sizeof(PacketHeader));
        T reconstructedState{};
        reconstructedState.DeserializeDelta(reader, baselineState);

        if (reader.HasOverflowed()) {
            return false;
        }

        // Step 4: Store reconstructed state into contiguous ring buffer
        m_ringBuffer.Insert(incomingSeq, reconstructedState);
        m_latestSequence = incomingSeq;
        m_hasReceivedState = true;

        outSequence = incomingSeq;
        outReconstructedState = reconstructedState;
        return true;
    }

    [[nodiscard]] const SnapshotRingBuffer<T, Capacity>& GetRingBuffer() const noexcept {
        return m_ringBuffer;
    }

    [[nodiscard]] std::optional<uint16_t> GetLatestSequence() const noexcept {
        if (!m_hasReceivedState) return std::nullopt;
        return m_latestSequence;
    }

    [[nodiscard]] const T* GetLatestState() const noexcept {
        if (!m_hasReceivedState) return nullptr;
        return m_ringBuffer.Get(m_latestSequence);
    }

    [[nodiscard]] uint32_t GetDroppedPacketsCount() const noexcept {
        return m_droppedPacketsCount;
    }

private:
    SnapshotRingBuffer<T, Capacity> m_ringBuffer;
    uint16_t m_latestSequence{0};
    bool     m_hasReceivedState{false};
    uint32_t m_droppedPacketsCount{0};
};

} // namespace DeltaCompression
