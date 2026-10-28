#pragma once

#include <cstdint>

namespace DeltaCompression {

enum PacketType : uint8_t {
    PACKET_TYPE_DELTA = 1,
    PACKET_TYPE_ACK   = 2
};

#pragma pack(push, 1)
// 5-byte packed UDP header for delta compression and ACK feedback
struct PacketHeader {
    uint8_t  packetType{PACKET_TYPE_DELTA};
    uint16_t sequenceNumber{0}; // For Delta: current snapshot seq. For ACK: client seq.
    uint16_t ackNumber{0};      // For Delta: baseline seq used. For ACK: snapshot seq being ACKed.
};
#pragma pack(pop)

static_assert(sizeof(PacketHeader) == 5, "PacketHeader must be exactly 5 bytes packed.");

} // namespace DeltaCompression
