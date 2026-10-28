# DeltaC: Bit-Packed Delta Compression Engine over UDP

  

**DeltaC** is a multiplayer network replication engine implemented in C++17 without external dependencies. It demonstrates core networking concepts used in fast-paced multiplayer architectures, including lossy float quantization, bit-packed dirty masks, and history-based delta diffing over raw, non-blocking UDP sockets.

  

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
                                                                                      Client Baseline)

```

  

---

  

## Core Components

  

### 1. Fixed-Size History Allocator (`SnapshotRingBuffer<T, 256>`)

  

*  **Memory Management:** Avoids runtime heap allocations (`new`, `std::vector::push_back`) in the replication loop to reduce memory fragmentation.

*  **Cache Locality:** The 256-slot ring buffer occupies $\approx  9.5\text{ KB}$, fitting within typical L1 data caches ($32\text{--}48\text{ KB}$) for efficient baseline state lookups.

*  **Index Calculation:** Uses a bitwise `AND` mask for modulo operations (`seq & 255`) rather than integer division.

*  **Sequence Arithmetic:** Handles sequence number wrap-around (every $65{,}536$ ticks) using RFC 1982 serial number arithmetic:

  

$$\text{SequenceGreaterThan}(S_1, S_2) \iff  \text{static\_cast<int16\_t>}(S_1 - S_2) > 0$$

  
  
  

### 2. Cross-Byte BitStream Serializer (`BitWriter` & `BitReader`)

  

*  **Bit Packing:** Packs variable-width data ($1\text{ to } 32\text{ bits}$) across byte boundaries using Least Significant Bit (LSB) first ordering.

*  **Safety Checks:** Includes guards against 32-bit shift overflows and masks unused high bits in source parameters.

*  **Bounds Checking:** Tracks bit capacity and sets an overflow flag on out-of-bounds writes to prevent memory corruption.

  

### 3. Coordinate Quantization (`Quantization.h`)

  

*  **Float Mapping:** Maps continuous 32-bit IEEE 754 floats $[-1000.0\text{ m}, +1000.0\text{ m}]$ to 16-bit unsigned integers $[0, 65535]$.

*  **Error Bounds:** Discrete step size is $\Delta = \frac{2000.0}{65535} \approx  3.05\text{ cm}$. Maximum theoretical roundoff error is strictly bounded to:

  

$$\epsilon_{\max} = \frac{\Delta}{2} \approx  1.5259\text{ cm}$$

  
  

*  **Euler Angles:** Quantizes rotations with parametric bit counts (e.g., Yaw in $10\text{ bits}$ $\approx  0.35^\circ$ precision).

  

### 4. Delta Differencing (`PhysicsState.h`)

  

*  **Discrete Comparison:** Compares fields in *quantized discrete space* rather than raw floats, preventing dirty mask triggers caused by floating-point micro-jitter below the quantization step size.

*  **Dirty Masks:** Only serializes fields that have changed relative to the acknowledged baseline.

  

### 5. UDP Protocol & Loss Handling

  

*  **Sockets:** Utilizes raw non-blocking UDP sockets (`ioctlsocket` on Windows, `fcntl` on POSIX).

*  **Wire Protocol:** Prepends a 5-byte header (`packetType`, `sequenceNumber`, `ackNumber`) to the bitstream.

*  **Loss Recovery:** If a packet drops, the client does not acknowledge the missing snapshot. The server automatically diffs subsequent ticks against the last successfully acknowledged baseline, allowing state recovery without requiring explicit retransmissions.

  

---

  

## Example Bandwidth Metrics

  

*(Note: Packet sizes include the 5-byte `PacketHeader`. Based on a 35-byte uncompressed state).*

  

| Replication Scenario | Active Dirty Fields | Payload Size | Packet Size | Bandwidth Savings |

| --- | --- | --- | --- | --- |

| **Idle / Stationary** | None ($0$) | $10\text{ bits}$ | $7\text{ B}$ | $96.4\%$ |

| **Damage Event** | Health only ($1$) | $26\text{ bits}$ | $9\text{ B}$ | $90.7\%$ |

| **Player Jump** | PosY, VelY, Flags ($3$) | $50\text{ bits}$ | $12\text{ B}$ | $82.1\%$ |

| **Full State Update** | All $10$ fields | $149\text{ bits}$ | $24\text{ B}$ | $46.8\%$ |

  

---

  

## Codebase Organization

  

```text

DeltaC/

├── include/

│ ├── BitReader.h # LSB-first bitstream unpacker with bounds checking

│ ├── BitWriter.h # LSB-first bitstream packer

│ ├── ClientReplicationManager.h # Delta decoding & state reconstruction logic

│ ├── GameState.h # Authoritative state definitions

│ ├── NetworkStateTraits.h # C++17 SFINAE trait validation

│ ├── PacketHeader.h # 5-byte UDP header definitions

│ ├── PhysicsState.h # Sample state with DirtyMask & quantization logic

│ ├── Quantization.h # Float-to-integer mapping functions

│ ├── SnapshotRingBuffer.h # Circular buffer with RFC 1982 math

│ └── UdpSocket.h # Cross-platform non-blocking socket wrapper

├── src/

│ └── main.cpp # Verification harness (20 Hz loop + packet loss sim)

└── README.md

  

```

  

---

  

## Build & Run Instructions

  

### Prerequisites

  

* C++17 compliant compiler (`GCC 9+`, `Clang 10+`, or `MSVC 2019+`).

* Windows: Links against `ws2_32.lib` (Winsock2).

* Linux / macOS: Standard POSIX sockets.

  

### Compilation

  

#### Windows (MinGW / GCC / Clang)

  

```powershell

g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 -Iinclude src/main.cpp -o DeltaC.exe -lws2_32

.\DeltaC.exe

  

```

  

#### Linux / macOS (GCC / Clang)

  

```bash

g++  -std=c++17  -Wall  -Wextra  -Wpedantic  -O2  -Iinclude  src/main.cpp  -o  DeltaC

./DeltaC

  

```

  

---

  

## Verification Test Output

  

The automated test harness validates the architecture, including sequence wrap-around and simulated packet loss over a local socket:

  

```text

==============================================================

DeltaC Test Harness - State Reconstruction & Loss Handling

==============================================================

  

[Test 1] Sequence Validation...

-> PASSED: Out-of-order, duplicate, and wrap-around sequences handled.

  

[Test 2] 20 Hz Replication with Simulated Packet Loss...

- Simulating 25 ticks. Packet #7 will be dropped.

  

[Network] Packet #7 dropped in simulation.

[Recovery] Client recovered on Packet #8 (diffed against Baseline #6).

  

[Metrics]

- Server Snapshots: 25

- Packets Dropped: 1

- Client Sequence: 24

-> PASSED: Reconstructed states match authoritative state within error bounds.

  

```

  

---

  

## State Serialization Interface

  

The engine uses C++17 SFINAE (`NetworkStateTraits.h`) to accept custom gameplay structs. To replicate a struct `T`, it must satisfy the following contract:

  

```cpp

struct  CustomState {

// 1. Serialization contract:

void  SerializeDelta(BitWriter&  writer, const  CustomState&  baseline) const  noexcept;

void  DeserializeDelta(BitReader&  reader, const  CustomState&  baseline) noexcept;

  

// 2. Structural requirements:

CustomState() =  default;

CustomState(const  CustomState&) =  default;

bool  operator==(const  CustomState&  other) const  noexcept;

};

  

// Validated at compile time:

static_assert(is_delta_serializable_v<CustomState>, "Must implement SerializeDelta & DeserializeDelta");

  

```

  

---

  

## License

  

MIT License.