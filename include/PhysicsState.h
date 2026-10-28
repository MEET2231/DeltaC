#pragma once

#include "NetworkStateTraits.h"
#include "Quantization.h"
#include <cstdint>
#include <cmath>

namespace DeltaCompression {

// Constants for world coordinate and kinematic bounds
namespace PhysicsBounds {
    constexpr float POS_MIN   = -1000.0f;
    constexpr float POS_MAX   =  1000.0f;
    constexpr int   POS_BITS  = 16;

    constexpr float VEL_MIN   = -100.0f;
    constexpr float VEL_MAX   =  100.0f;
    constexpr int   VEL_BITS  = 16;

    constexpr float YAW_MIN   = 0.0f;
    constexpr float YAW_MAX   = 360.0f;
    constexpr int   YAW_BITS  = 10;

    constexpr float PITCH_MIN = -90.0f;
    constexpr float PITCH_MAX = 90.0f;
    constexpr int   PITCH_BITS = 9;

    constexpr int   HEALTH_BITS = 16;
    constexpr int   FLAGS_BITS  = 8;
}

// Field indices for the bitfield DirtyMask
enum PhysicsField : uint8_t {
    FIELD_POS_X = 0,
    FIELD_POS_Y,
    FIELD_POS_Z,
    FIELD_VEL_X,
    FIELD_VEL_Y,
    FIELD_VEL_Z,
    FIELD_YAW,
    FIELD_PITCH,
    FIELD_HEALTH,
    FIELD_FLAGS,
    FIELD_COUNT // 10 fields total -> 10-bit DirtyMask
};

#pragma pack(push, 1)
struct PhysicsState {
    // Kinematic Transforms (32-bit floats)
    float posX{0.0f};
    float posY{0.0f};
    float posZ{0.0f};

    float velX{0.0f};
    float velY{0.0f};
    float velZ{0.0f};

    float yaw{0.0f};
    float pitch{0.0f};

    // Attributes & Flags
    uint16_t health{100};
    uint8_t  flags{0};

    // State equality for baseline diffing
    bool operator==(const PhysicsState& other) const noexcept {
        return posX == other.posX &&
               posY == other.posY &&
               posZ == other.posZ &&
               velX == other.velX &&
               velY == other.velY &&
               velZ == other.velZ &&
               yaw  == other.yaw  &&
               pitch == other.pitch &&
               health == other.health &&
               flags == other.flags;
    }

    bool operator!=(const PhysicsState& other) const noexcept {
        return !(*this == other);
    }

    // Phase 4: SerializeDelta generates a bitfield DirtyMask, serializes it,
    // and conditionally serializes only the changed (dirty) fields.
    void SerializeDelta(BitWriter& writer, const PhysicsState& baseline) const noexcept {
        using namespace PhysicsBounds;

        // Precompute quantized values
        const uint16_t qPosX = QuantizeFloat(posX, POS_MIN, POS_MAX);
        const uint16_t qPosY = QuantizeFloat(posY, POS_MIN, POS_MAX);
        const uint16_t qPosZ = QuantizeFloat(posZ, POS_MIN, POS_MAX);

        const uint16_t qVelX = QuantizeFloat(velX, VEL_MIN, VEL_MAX);
        const uint16_t qVelY = QuantizeFloat(velY, VEL_MIN, VEL_MAX);
        const uint16_t qVelZ = QuantizeFloat(velZ, VEL_MIN, VEL_MAX);

        const uint32_t qYaw   = QuantizeFloatBits(yaw, YAW_MIN, YAW_MAX, YAW_BITS);
        const uint32_t qPitch = QuantizeFloatBits(pitch, PITCH_MIN, PITCH_MAX, PITCH_BITS);

        const uint16_t bPosX = QuantizeFloat(baseline.posX, POS_MIN, POS_MAX);
        const uint16_t bPosY = QuantizeFloat(baseline.posY, POS_MIN, POS_MAX);
        const uint16_t bPosZ = QuantizeFloat(baseline.posZ, POS_MIN, POS_MAX);

        const uint16_t bVelX = QuantizeFloat(baseline.velX, VEL_MIN, VEL_MAX);
        const uint16_t bVelY = QuantizeFloat(baseline.velY, VEL_MIN, VEL_MAX);
        const uint16_t bVelZ = QuantizeFloat(baseline.velZ, VEL_MIN, VEL_MAX);

        const uint32_t bYaw   = QuantizeFloatBits(baseline.yaw, YAW_MIN, YAW_MAX, YAW_BITS);
        const uint32_t bPitch = QuantizeFloatBits(baseline.pitch, PITCH_MIN, PITCH_MAX, PITCH_BITS);

        // 1. Generate DirtyMask (bitfield of size FIELD_COUNT)
        uint16_t dirtyMask = 0;

        if (qPosX != bPosX)                 dirtyMask |= (1U << FIELD_POS_X);
        if (qPosY != bPosY)                 dirtyMask |= (1U << FIELD_POS_Y);
        if (qPosZ != bPosZ)                 dirtyMask |= (1U << FIELD_POS_Z);
        if (qVelX != bVelX)                 dirtyMask |= (1U << FIELD_VEL_X);
        if (qVelY != bVelY)                 dirtyMask |= (1U << FIELD_VEL_Y);
        if (qVelZ != bVelZ)                 dirtyMask |= (1U << FIELD_VEL_Z);
        if (qYaw  != bYaw)                  dirtyMask |= (1U << FIELD_YAW);
        if (qPitch != bPitch)               dirtyMask |= (1U << FIELD_PITCH);
        if (health != baseline.health)      dirtyMask |= (1U << FIELD_HEALTH);
        if (flags  != baseline.flags)       dirtyMask |= (1U << FIELD_FLAGS);

        // 2. Serialize the DirtyMask (10 bits)
        writer.WriteBits(dirtyMask, FIELD_COUNT);

        // 3. Conditionally serialize changed fields
        if (dirtyMask & (1U << FIELD_POS_X))  writer.WriteBits(qPosX, POS_BITS);
        if (dirtyMask & (1U << FIELD_POS_Y))  writer.WriteBits(qPosY, POS_BITS);
        if (dirtyMask & (1U << FIELD_POS_Z))  writer.WriteBits(qPosZ, POS_BITS);
        if (dirtyMask & (1U << FIELD_VEL_X))  writer.WriteBits(qVelX, VEL_BITS);
        if (dirtyMask & (1U << FIELD_VEL_Y))  writer.WriteBits(qVelY, VEL_BITS);
        if (dirtyMask & (1U << FIELD_VEL_Z))  writer.WriteBits(qVelZ, VEL_BITS);
        if (dirtyMask & (1U << FIELD_YAW))    writer.WriteBits(qYaw, YAW_BITS);
        if (dirtyMask & (1U << FIELD_PITCH))  writer.WriteBits(qPitch, PITCH_BITS);
        if (dirtyMask & (1U << FIELD_HEALTH)) writer.WriteBits(health, HEALTH_BITS);
        if (dirtyMask & (1U << FIELD_FLAGS))  writer.WriteBits(flags, FLAGS_BITS);
    }

    // Deserializes changed fields against baseline from BitReader
    void DeserializeDelta(BitReader& reader, const PhysicsState& baseline) noexcept {
        using namespace PhysicsBounds;

        // Start from baseline state
        *this = baseline;

        // 1. Read DirtyMask (10 bits)
        const uint16_t dirtyMask = static_cast<uint16_t>(reader.ReadBits(FIELD_COUNT));

        // 2. Decode only fields whose dirty bit is 1
        if (dirtyMask & (1U << FIELD_POS_X)) {
            const uint16_t q = static_cast<uint16_t>(reader.ReadBits(POS_BITS));
            posX = DequantizeFloat(q, POS_MIN, POS_MAX);
        }
        if (dirtyMask & (1U << FIELD_POS_Y)) {
            const uint16_t q = static_cast<uint16_t>(reader.ReadBits(POS_BITS));
            posY = DequantizeFloat(q, POS_MIN, POS_MAX);
        }
        if (dirtyMask & (1U << FIELD_POS_Z)) {
            const uint16_t q = static_cast<uint16_t>(reader.ReadBits(POS_BITS));
            posZ = DequantizeFloat(q, POS_MIN, POS_MAX);
        }
        if (dirtyMask & (1U << FIELD_VEL_X)) {
            const uint16_t q = static_cast<uint16_t>(reader.ReadBits(VEL_BITS));
            velX = DequantizeFloat(q, VEL_MIN, VEL_MAX);
        }
        if (dirtyMask & (1U << FIELD_VEL_Y)) {
            const uint16_t q = static_cast<uint16_t>(reader.ReadBits(VEL_BITS));
            velY = DequantizeFloat(q, VEL_MIN, VEL_MAX);
        }
        if (dirtyMask & (1U << FIELD_VEL_Z)) {
            const uint16_t q = static_cast<uint16_t>(reader.ReadBits(VEL_BITS));
            velZ = DequantizeFloat(q, VEL_MIN, VEL_MAX);
        }
        if (dirtyMask & (1U << FIELD_YAW)) {
            const uint32_t q = reader.ReadBits(YAW_BITS);
            yaw = DequantizeFloatBits(q, YAW_MIN, YAW_MAX, YAW_BITS);
        }
        if (dirtyMask & (1U << FIELD_PITCH)) {
            const uint32_t q = reader.ReadBits(PITCH_BITS);
            pitch = DequantizeFloatBits(q, PITCH_MIN, PITCH_MAX, PITCH_BITS);
        }
        if (dirtyMask & (1U << FIELD_HEALTH)) {
            health = static_cast<uint16_t>(reader.ReadBits(HEALTH_BITS));
        }
        if (dirtyMask & (1U << FIELD_FLAGS)) {
            flags = static_cast<uint8_t>(reader.ReadBits(FLAGS_BITS));
        }
    }
};
#pragma pack(pop)

static_assert(is_delta_serializable_v<PhysicsState>,
              "PhysicsState must implement SerializeDelta and DeserializeDelta.");

} // namespace DeltaCompression
