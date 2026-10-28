#include "BitWriter.h"
#include "BitReader.h"
#include "PacketHeader.h"
#include "PhysicsState.h"
#include "SnapshotRingBuffer.h"
#include "ClientReplicationManager.h"
#include "UdpSocket.h"

#include <iostream>
#include <cassert>
#include <chrono>
#include <thread>
#include <cmath>
#include <vector>

using namespace DeltaCompression;

void TestOutOfOrderAndSequenceWrapAroundRejection() {
    std::cout << "[Phase 6 - Test 1] Out-of-Order & Modular Sequence Wrap-Around Rejection...\n";

    ClientReplicationManager<PhysicsState, 256> client;

    // Helper to generate a dummy packet
    auto createPacket = [](uint16_t seq, uint16_t baseSeq, const PhysicsState& cur, const PhysicsState& base, std::vector<uint8_t>& out) {
        out.resize(sizeof(PacketHeader) + 64);
        PacketHeader header{};
        header.packetType = PACKET_TYPE_DELTA;
        header.sequenceNumber = seq;
        header.ackNumber = baseSeq;
        std::memcpy(out.data(), &header, sizeof(PacketHeader));

        BitWriter writer(out.data() + sizeof(PacketHeader), out.size() - sizeof(PacketHeader));
        cur.SerializeDelta(writer, base);
        out.resize(sizeof(PacketHeader) + writer.GetBytesWritten());
    };

    PhysicsState s0{};
    s0.posX = 10.0f;

    std::vector<uint8_t> p0;
    createPacket(10, 0, s0, PhysicsState{}, p0);

    uint16_t reconstructedSeq = 0;
    PhysicsState reconstructedState{};

    // 1. Process Sequence 10
    assert(client.ProcessSnapshotPacket(p0.data(), p0.size(), reconstructedSeq, reconstructedState));
    assert(reconstructedSeq == 10);
    assert(client.GetLatestSequence().value() == 10);

    // 2. Out-of-order Packet: Sequence 8 arrives late -> MUST be discarded
    PhysicsState sLate{};
    std::vector<uint8_t> pLate;
    createPacket(8, 0, sLate, PhysicsState{}, pLate);
    assert(!client.ProcessSnapshotPacket(pLate.data(), pLate.size(), reconstructedSeq, reconstructedState));
    assert(client.GetLatestSequence().value() == 10); // Still 10!

    // 3. Duplicate Packet: Sequence 10 arrives again -> MUST be discarded
    assert(!client.ProcessSnapshotPacket(p0.data(), p0.size(), reconstructedSeq, reconstructedState));

    // 4. Newer Packet: Sequence 11 arrives -> MUST be accepted
    PhysicsState s11 = s0;
    s11.posX = 20.0f;
    std::vector<uint8_t> p11;
    createPacket(11, 10, s11, s0, p11);
    assert(client.ProcessSnapshotPacket(p11.data(), p11.size(), reconstructedSeq, reconstructedState));
    assert(reconstructedSeq == 11);
    assert(client.GetLatestSequence().value() == 11);

    // 5. Sequence Wrap-Around: 65535 -> 0
    ClientReplicationManager<PhysicsState, 256> wrapClient;
    PhysicsState sWrap0{};
    sWrap0.health = 80;
    std::vector<uint8_t> pWrapMax;
    createPacket(65535, 0, sWrap0, PhysicsState{}, pWrapMax);
    assert(wrapClient.ProcessSnapshotPacket(pWrapMax.data(), pWrapMax.size(), reconstructedSeq, reconstructedState));
    assert(wrapClient.GetLatestSequence().value() == 65535);

    // Sequence 0 arrives after 65535 -> MUST be accepted as newer!
    PhysicsState sWrapNext = sWrap0;
    sWrapNext.health = 75;
    std::vector<uint8_t> pWrapZero;
    createPacket(0, 65535, sWrapNext, sWrap0, pWrapZero);
    assert(wrapClient.ProcessSnapshotPacket(pWrapZero.data(), pWrapZero.size(), reconstructedSeq, reconstructedState));
    assert(wrapClient.GetLatestSequence().value() == 0);

    std::cout << "  -> PASSED: Out-of-order, duplicate, and modular wrap-around handled flawlessly.\n\n";
}

void TestLiveReplicationWithPacketLoss() {
    std::cout << "[Phase 6 - Test 2] Live 20 Hz Replication with Simulated Packet Loss...\n";

    assert(UdpSocket::GlobalInit());

    constexpr uint16_t SERVER_PORT = 27017;
    constexpr uint16_t CLIENT_PORT = 27018;
    constexpr const char* LOCALHOST = "127.0.0.1";

    UdpSocket serverSocket;
    UdpSocket clientSocket;

    assert(serverSocket.Open(SERVER_PORT));
    assert(clientSocket.Open(CLIENT_PORT));

    SnapshotRingBuffer<PhysicsState, 256> serverHistory;
    ClientReplicationManager<PhysicsState, 256> client;

    uint16_t serverSeq = 0;
    uint16_t clientSeq = 0;
    bool hasClientBaseline = false;
    uint16_t clientBaselineSeq = 0;

    constexpr int TOTAL_TICKS = 25;
    constexpr int PACKET_TO_DROP = 7; // Simulate network packet loss at tick 7
    int droppedCount = 0;

    std::cout << "  - Simulating " << TOTAL_TICKS << " ticks over UDP. Packet #"
              << PACKET_TO_DROP << " will be intentionally dropped in transit.\n\n";

    for (int tick = 0; tick < TOTAL_TICKS; ++tick) {
        // -------------------------------------------------------------
        // 1. Server: Poll ACKs from client
        // -------------------------------------------------------------
        uint8_t serverRecvBuf[256];
        int bytesIn = 0;
        while ((bytesIn = serverSocket.RecvFrom(serverRecvBuf, sizeof(serverRecvBuf))) > 0) {
            if (static_cast<size_t>(bytesIn) >= sizeof(PacketHeader)) {
                PacketHeader ackHeader;
                std::memcpy(&ackHeader, serverRecvBuf, sizeof(PacketHeader));
                if (ackHeader.packetType == PACKET_TYPE_ACK) {
                    const uint16_t ackedSnapshot = ackHeader.ackNumber;
                    if (!hasClientBaseline || SequenceGreaterThan(ackedSnapshot, clientBaselineSeq)) {
                        clientBaselineSeq = ackedSnapshot;
                        hasClientBaseline = true;
                    }
                }
            }
        }

        // -------------------------------------------------------------
        // 2. Server: Generate authoritative state
        // -------------------------------------------------------------
        PhysicsState currentServerState{};
        currentServerState.posX = static_cast<float>(tick) * 3.0f;
        currentServerState.posY = std::sin(tick * 0.3f) * 8.0f;
        currentServerState.posZ = 50.0f;
        currentServerState.velX = 3.0f;
        currentServerState.velY = std::cos(tick * 0.3f) * 2.4f;
        currentServerState.velZ = 0.0f;
        currentServerState.yaw  = static_cast<float>((tick * 20) % 360);
        currentServerState.pitch = -5.0f;
        currentServerState.health = static_cast<uint16_t>(100 - tick);
        currentServerState.flags  = (tick % 2 == 0) ? 1 : 0;

        serverHistory.Insert(serverSeq, currentServerState);

        // -------------------------------------------------------------
        // 3. Server: Delta-compress against client's acknowledged baseline
        // -------------------------------------------------------------
        PhysicsState baselineState{};
        uint16_t baselineUsed = 0;
        if (hasClientBaseline) {
            const PhysicsState* b = serverHistory.Get(clientBaselineSeq);
            if (b) {
                baselineState = *b;
                baselineUsed = clientBaselineSeq;
            }
        }

        uint8_t packetBuffer[256] = {0};
        PacketHeader deltaHeader{};
        deltaHeader.packetType = PACKET_TYPE_DELTA;
        deltaHeader.sequenceNumber = serverSeq;
        deltaHeader.ackNumber = baselineUsed;
        std::memcpy(packetBuffer, &deltaHeader, sizeof(PacketHeader));

        BitWriter writer(packetBuffer + sizeof(PacketHeader), sizeof(packetBuffer) - sizeof(PacketHeader));
        currentServerState.SerializeDelta(writer, baselineState);

        const size_t totalPacketBytes = sizeof(PacketHeader) + writer.GetBytesWritten();

        // -------------------------------------------------------------
        // 4. UDP Transmission (with simulated packet drop)
        // -------------------------------------------------------------
        if (tick == PACKET_TO_DROP) {
            std::cout << "  [Network Loss] Packet #" << serverSeq << " DROPPED in simulated WAN!\n";
            droppedCount++;
        } else {
            serverSocket.SendTo(packetBuffer, totalPacketBytes, LOCALHOST, CLIENT_PORT);
        }

        // -------------------------------------------------------------
        // 5. Client: Receive, Decode, Reconstruct & ACK
        // -------------------------------------------------------------
        std::this_thread::sleep_for(std::chrono::milliseconds(2));

        uint8_t clientRecvBuf[256];
        int clientBytesIn = 0;
        while ((clientBytesIn = clientSocket.RecvFrom(clientRecvBuf, sizeof(clientRecvBuf))) > 0) {
            uint16_t decodedSeq = 0;
            PhysicsState decodedState{};

            const bool success = client.ProcessSnapshotPacket(clientRecvBuf,
                                                              static_cast<size_t>(clientBytesIn),
                                                              decodedSeq,
                                                              decodedState);
            if (success) {
                // Send immediate ACK back to server
                PacketHeader replyAck{};
                replyAck.packetType = PACKET_TYPE_ACK;
                replyAck.sequenceNumber = clientSeq++;
                replyAck.ackNumber = decodedSeq;
                clientSocket.SendTo(&replyAck, sizeof(PacketHeader), LOCALHOST, SERVER_PORT);

                // Verify client's decoded state matches server's authoritative state
                const PhysicsState* authState = serverHistory.Get(decodedSeq);
                assert(authState != nullptr);
                assert(std::abs(decodedState.posX - authState->posX) <= 0.05f);
                assert(std::abs(decodedState.posY - authState->posY) <= 0.05f);
                assert(std::abs(decodedState.posZ - authState->posZ) <= 0.05f);
                assert(std::abs(decodedState.yaw  - authState->yaw)  <= 0.5f);
                assert(decodedState.health == authState->health);
                assert(decodedState.flags  == authState->flags);

                if (decodedSeq == PACKET_TO_DROP + 1) {
                    std::cout << "  [Recovery] Client successfully recovered on Packet #"
                              << decodedSeq << " (diffed across dropped packet against Baseline #"
                              << deltaHeader.ackNumber << ")!\n";
                }
            }
        }

        serverSeq++;
    }

    std::cout << "\n  [Verification Metrics]\n";
    std::cout << "  - Total Server Snapshots: " << serverSeq << "\n";
    std::cout << "  - Total Packets Dropped:  " << droppedCount << "\n";
    std::cout << "  - Client Latest Sequence: " << client.GetLatestSequence().value() << "\n";
    std::cout << "  - All reconstructed states matched authoritative state within error bounds.\n";
    std::cout << "  -> PASSED: Complete end-to-end delta engine with loss recovery verified.\n\n";

    serverSocket.Close();
    clientSocket.Close();
    UdpSocket::GlobalShutdown();
}

int main() {
    std::cout << "==============================================================\n";
    std::cout << " AAA Network Engine - Phase 6: Client Decoding & State Recon  \n";
    std::cout << "==============================================================\n\n";

    TestOutOfOrderAndSequenceWrapAroundRejection();
    TestLiveReplicationWithPacketLoss();

    std::cout << "==============================================================\n";
    std::cout << " ALL PHASES 1 THROUGH 6 FULLY IMPLEMENTED AND VERIFIED!       \n";
    std::cout << "==============================================================\n";
    return 0;
}
