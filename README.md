# DeltaC: High-Performance Bit-Packed Delta Compression Engine over UDP

[![Standard](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux-lightgrey.svg)]()
[![Zero Allocation](https://img.shields.io/badge/Allocation-Zero%20Runtime%20Heap-brightgreen.svg)]()

**DeltaC** is a production-grade, low-latency multiplayer network replication engine implemented in pure modern **C++17** without external dependencies (no Boost or third-party wrappers). It implements the core networking architecture utilized by AAA multiplayer titles (e.g., EA Frostbite, Respawn Titanfall/Apex, Valve Source Engine), combining **lossy float quantization**, **bit-packed dirty masks**, and **history-based baseline delta diffing** over raw, non-blocking UDP sockets.

---

## Architecture Overview

```
                                      SERVER (20 Hz Tick)
                                                |
                             [Authoritative Current State #S]
                                                |
                                                v
    [Acknowledged Baseline #B] ------> [Field Diff Engine]
    (From 256-Slot RingBuffer)                  |
                                                v
                                    [10-bit Dirty Bitmask]
                                                |
                                                v
                              [BitWriter: LSB-First BitStream]
                               (Quantized Floats & Attributes)
                                                |
                                                v
                                  [PacketHeader: Seq #S, Base #B]
                                                |
                                      (Raw UDP Datagram)
                                                |
                                                v
==================================== UNRELIABLE INTERNET / WAN ====================================
                                                |
                                                v
                                      CLIENT (Replication)
                                                |
                                   [ProcessSnapshotPacket()]
                                                |
             +----------------------------------+----------------------------------+
             |                                                                     |
             v                                                                     v
  [RFC 1982 Sequence Check]                                            [Fetch Baseline #B from]
  (Discard Out-of-Order / Duplicates)                                    [Local RingBuffer]
             |                                                                     |
             +----------------------------------+----------------------------------+
                                                |
                                                v
                                  [BitReader: DeserializeDelta]
                                  (Dequantize Dirty Fields Only)
                                                |
                                                v
                               [Reconstructed Authoritative State]
                               (Stored in Local Client RingBuffer)
                                                |
                                                v
                                  [Immediate UDP ACK for #S] ----------------------> (Server Updates
                                                                                      Client Baseline!)
```

---

## Key Technical Innovations & Memory Engineering

### 1. Zero-Allocation History Allocator (`SnapshotRingBuffer<T, 256>`)
* **Strict $O(1)$ Runtime Memory:** Avoids heap allocations (`malloc`, `new`, `std::vector::push_back`) in the replication tick to eliminate memory fragmentation and OS allocator contention.
* **L1 Cache Locality:** The 256-slot ring buffer occupies **$\approx 9.5\text{ KB}$**, fitting entirely within the CPU's L1 Data Cache ($32\text{--}48\text{ KB}$ per core), guaranteeing zero cold cache misses during baseline state lookups.
* **Branch-Free Bitwise Modulo:** With capacity $N = 256$, integer division (`seq % 256`, which consumes $10\text{--}25$ CPU cycles) is replaced with a single-cycle bitwise `AND` mask:
  $$\text{Index}(S) = S \ \& \ 255$$
* **RFC 1982 Serial Number Arithmetic:** Sequence numbers wrap every $65{,}536$ ticks ($54.6\text{ minutes}$ at $20\text{ Hz}$). Distance and order are evaluated using signed half-space deltas:
  $$\text{SequenceGreaterThan}(S_1, S_2) \iff \text{static\_cast<int16\_t>}(S_1 - S_2) > 0$$

### 2. Cross-Byte BitStream Serializer (`BitWriter` & `BitReader`)
* **Arbitrary Bit-Width Packing:** Packs variable-width data ($1\text{ to } 32\text{ bits}$) across byte boundaries using Least Significant Bit (LSB) first ordering.
* **Undefined Behavior Prevention:** Guards against 32-bit shift overflows (`1U << 32` UB in C++).
* **High-Bit Sanitization:** Masks unused high bits in source parameters to prevent bitstream pollution.
* **Explicit Boundary Safety:** Tracks bit capacity and trips an overflow flag on out-of-bounds writes without memory corruption.

### 3. Lossy Coordinate Quantization (`Quantization.h`)
* **$50.0\%$ Bandwidth Reduction:** Maps continuous 32-bit IEEE 754 floats $[-1000.0\text{ m}, +1000.0\text{ m}]$ to 16-bit unsigned integers $[0, 65535]$.
* **Bounded Precision Loss:** Discrete step size $\Delta = \frac{2000.0}{65535} \approx 3.05\text{ cm}$. Maximum theoretical roundoff error is strictly bounded to half a step:
  $$\epsilon_{\max} = \frac{\Delta}{2} \approx 1.5259\text{ cm}$$
* **Multi-Resolution Rotations:** Quantizes Euler angles with parametric bit counts (Yaw in $10\text{ bits}$ $\approx 0.35^\circ$, Pitch in $9\text{ bits}$ $\approx 0.35^\circ$).

### 4. DirtyMask Delta Differencing (`PhysicsState.h`)
* **Quantized Differencing:** Compares fields in *quantized discrete space* rather than raw floats, preventing "phantom dirty bits" caused by micro-jitter below the quantization step size.
* **Extreme Bandwidth Compression:**
  * **Stationary / Idle Entity:** **$10\text{ bits}$** ($1.25\text{ bytes}$) vs $280\text{ bits}$ uncompressed $\implies$ **$96.4\%$ bandwidth reduction**.
  * **Typical Movement (Jump/Run):** **$50\text{ bits}$** ($6.25\text{ bytes}$) vs $280\text{ bits}$ uncompressed $\implies$ **$82.1\%$ bandwidth reduction**.
  * **Worst-Case (All 10 Fields Dirty):** **$149\text{ bits}$** ($18.6\text{ bytes}$) vs $280\text{ bits}$ uncompressed $\implies$ **$46.8\%$ bandwidth reduction**.

### 5. Non-Blocking UDP Wire Protocol & Packet Loss Resilience
* **Asynchronous Sockets:** Raw non-blocking UDP sockets (`ioctlsocket` on Windows, `fcntl` on POSIX) handle datagram bursts without frame hitches.
* **Wire Protocol Header:** Compact 5-byte packed header (`packetType`, `sequenceNumber`, `ackNumber`).
* **Loss-Tolerant Baseline Tracking:** When packets drop in transit, the client does not ACK the missing snapshot. The server automatically continues diffing subsequent ticks against the last acknowledged baseline, recovering immediately without TCP-style retransmission delays.

---

## Bandwidth Benchmark Metrics

| Replication Scenario | Active Dirty Fields | Payload Size (Bits) | Packet Size (Bytes) | Uncompressed Size | Bandwidth Savings |
| :--- | :--- | :---: | :---: | :---: | :---: |
| **Idle / Stationary** | None ($0$) | **$10\text{ bits}$** | **$7\text{ B}$** | $35\text{ B}$ | **$96.4\%$** |
| **Damage Event** | Health only ($1$) | **$26\text{ bits}$** | **$9\text{ B}$** | $35\text{ B}$ | **$90.7\%$** |
| **Player Jump** | PosY, VelY, Flags ($3$) | **$50\text{ bits}$** | **$12\text{ B}$** | $35\text{ B}$ | **$82.1\%$** |
| **Full Combat State** | All $10$ fields | **$149\text{ bits}$** | **$24\text{ B}$** | $35\text{ B}$ | **$46.8\%$** |

*(Packet sizes include the 5-byte `PacketHeader`).*

---

## Codebase Organization

```
DeltaC/
├── include/
│   ├── BitReader.h                 # LSB-first bitstream unpacker with overflow checks
│   ├── BitWriter.h                 # LSB-first bitstream packer with cross-byte shifts
│   ├── ClientReplicationManager.h  # Out-of-order rejection, delta decoding & state reconstruction
│   ├── GameState.h                 # Authoritative state reference
│   ├── NetworkStateTraits.h        # C++17 SFINAE compile-time delta serialization contract
│   ├── PacketHeader.h              # 5-byte wire protocol header (seq, ack, packetType)
│   ├── PhysicsState.h              # Gameplay state with 10-bit DirtyMask & quantization bounds
│   ├── Quantization.h              # Fixed-precision float-to-integer mapping & inverse math
│   ├── SnapshotRingBuffer.h        # Templated, cache-aligned circular buffer with RFC 1982 math
│   └── UdpSocket.h                 # Cross-platform raw non-blocking UDP socket wrapper
├── src/
│   └── main.cpp                    # End-to-end verification harness (20 Hz loop + packet loss test)
└── README.md
```

---

## Build & Run Instructions

### Prerequisites
* Any C++17 compliant compiler: `GCC 9+`, `Clang 10+`, or `MSVC 2019+`.
* Windows: Links against `ws2_32.lib` (Winsock2).
* Linux / macOS: Standard POSIX sockets (no extra libraries required).

### Compilation

#### Windows (MinGW / GCC)
```powershell
g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 -Iinclude src/main.cpp -o DeltaC.exe -lws2_32
.\DeltaC.exe
```

#### Windows (Clang)
```powershell
clang++ -std=c++17 -Wall -Wextra -Wpedantic -O2 -Iinclude src/main.cpp -o DeltaC.exe -lws2_32
.\DeltaC.exe
```

#### Linux / macOS (GCC / Clang)
```bash
g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 -Iinclude src/main.cpp -o DeltaC
./DeltaC
```

---

## Verification Test Output

The automated test harness validates all 6 phases:

```text
==============================================================
 AAA Network Engine - Phase 6: Client Decoding & State Recon  
==============================================================

[Phase 6 - Test 1] Out-of-Order & Modular Sequence Wrap-Around Rejection...
  -> PASSED: Out-of-order, duplicate, and modular wrap-around handled flawlessly.

[Phase 6 - Test 2] Live 20 Hz Replication with Simulated Packet Loss...
  - Simulating 25 ticks over UDP. Packet #7 will be intentionally dropped in transit.

  [Network Loss] Packet #7 DROPPED in simulated WAN!
  [Recovery] Client successfully recovered on Packet #8 (diffed across dropped packet against Baseline #6)!

  [Verification Metrics]
  - Total Server Snapshots: 25
  - Total Packets Dropped:  1
  - Client Latest Sequence: 24
  - All reconstructed states matched authoritative state within error bounds.
  -> PASSED: Complete end-to-end delta engine with loss recovery verified.

==============================================================
 ALL PHASES 1 THROUGH 6 FULLY IMPLEMENTED AND VERIFIED!       
==============================================================
```

---

## Technical Contract on User-Defined Struct $T$

The engine is completely decoupled from gameplay state schemas via C++17 SFINAE traits (`NetworkStateTraits.h`). Any gameplay struct $T$ can be replicated through the engine by implementing:

```cpp
struct CustomState {
    // 1. Serialization contract:
    void SerializeDelta(BitWriter& writer, const CustomState& baseline) const noexcept;
    void DeserializeDelta(BitReader& reader, const CustomState& baseline) noexcept;

    // 2. Structural requirements:
    CustomState() = default;
    CustomState(const CustomState&) = default;
    bool operator==(const CustomState& other) const noexcept;
};

// Validated at compile time:
static_assert(is_delta_serializable_v<CustomState>, "Must implement SerializeDelta & DeserializeDelta");
```

---

## License
MIT License. Created as a AAA-caliber multiplayer networking engineering portfolio project.
