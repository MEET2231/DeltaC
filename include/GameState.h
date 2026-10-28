#pragma once

#include <cstdint>
#include <cmath>

namespace DeltaCompression {

// Bitmask flags for boolean status effects
enum StatusFlags : uint8_t {
    STATUS_NONE         = 0,
    STATUS_CROUCHING    = 1 << 0,
    STATUS_SPRINTING    = 1 << 1,
    STATUS_FIRING       = 1 << 2,
    STATUS_RELOADING    = 1 << 3,
    STATUS_ON_GROUND    = 1 << 4
};

// Enumeration of fields for dirty mask tracking in Phase 4
enum class GameStateField : uint8_t {
    PositionX = 0,
    PositionY,
    PositionZ,
    VelocityX,
    VelocityY,
    VelocityZ,
    Yaw,
    Pitch,
    Health,
    Shield,
    WeaponId,
    Ammo,
    StatusFlags,
    COUNT
};

#pragma pack(push, 1)
// Authoritative Game State containing variables of varying frequency
struct GameState {
    // High Frequency: Transforms & Kinematics (32-bit floats)
    float posX{0.0f};
    float posY{0.0f};
    float posZ{0.0f};

    float velX{0.0f};
    float velY{0.0f};
    float velZ{0.0f};

    float yaw{0.0f};    // Degrees [0.0f, 360.0f)
    float pitch{0.0f};  // Degrees [-90.0f, 90.0f]

    // Medium Frequency: Player Combat Attributes (16-bit / 8-bit unsigned integers)
    uint16_t health{100};
    uint16_t shield{100};
    uint16_t ammo{30};
    uint8_t  weaponId{1};

    // Low Frequency: Discrete Status Booleans packed into a single byte
    uint8_t  statusFlags{STATUS_ON_GROUND};

    // Helper functions for boolean status flags
    [[nodiscard]] constexpr bool HasStatus(StatusFlags flag) const noexcept {
        return (statusFlags & flag) != 0;
    }

    constexpr void SetStatus(StatusFlags flag, bool active) noexcept {
        if (active) {
            statusFlags |= static_cast<uint8_t>(flag);
        } else {
            statusFlags &= ~static_cast<uint8_t>(flag);
        }
    }

    // Exact equality operator used for state comparison and delta generation
    bool operator==(const GameState& other) const noexcept {
        return posX == other.posX &&
               posY == other.posY &&
               posZ == other.posZ &&
               velX == other.velX &&
               velY == other.velY &&
               velZ == other.velZ &&
               yaw  == other.yaw  &&
               pitch == other.pitch &&
               health == other.health &&
               shield == other.shield &&
               ammo == other.ammo &&
               weaponId == other.weaponId &&
               statusFlags == other.statusFlags;
    }

    bool operator!=(const GameState& other) const noexcept {
        return !(*this == other);
    }
};
#pragma pack(pop)

static_assert(sizeof(GameState) == 40, "GameState layout must be tightly packed to exactly 40 bytes.");

} // namespace DeltaCompression
