#pragma once
#include <cstdint>
#include <vector>

// Platform-specific includes for network ordering
#if defined(_WIN32)
    #include <winsock2.h>
#elif defined(__linux__)
    #include <arpa/inet.h>
#endif

namespace Flags {
    constexpr uint8_t CHECK_ENGINE = 1 << 0; 
    constexpr uint8_t OVERHEAT     = 1 << 1; 
    constexpr uint8_t LOW_BATTERY  = 1 << 2; 
    constexpr uint8_t ABS_ACTIVE   = 1 << 3; 
    constexpr uint8_t TCS_ACTIVE   = 1 << 4; 
    constexpr uint8_t REMOTE_KILL  = 1 << 5;
}

#pragma pack(push, 1)

struct Packet {
    // --- Header (16 Bytes) ---
    // Magic header for quick protocol verification
    uint16_t magic;       // 0xD350 (2 bytes)
    
    uint16_t vehicle_id;  // 2 bytes
    uint32_t sequence_id; // 4 bytes 
    uint64_t timestamp;   // 8 bytes

    // --- Physics Payload (8 Bytes) ---
    uint16_t rpm;         // 2 bytes
    uint16_t speed;       // 2 bytes
    
    // required for safety monitoring metrics
    int16_t  jerk;        // 2 bytes (Signed: can be negative deceleration)
    
    uint8_t  temp;        // 1 byte
    uint8_t  battery_level;// 1 byte

    // --- System Diagnostics (8 Bytes) ---
    uint8_t  gear;        // 1 byte
    uint8_t  flags;       // 1 byte
    uint8_t  version;     // 1 byte
    uint8_t  cpu_load;    // 1 byte
    uint16_t crc16;       // 2 bytes
    
    uint8_t  reserved[2]; // 2 bytes (Padding to reach 32)

    // --- Serialization Logic ---
    // "Strict Serialization: All integers bit-shifted to Big Endian"
    void serialize(std::vector<uint8_t>& buffer) const {
        buffer.resize(32);
        uint8_t* ptr = buffer.data();

        // 1. MAGIC (0xD350)
        ptr[0] = (magic >> 8) & 0xFF;
        ptr[1] = magic & 0xFF;

        // 2. Vehicle ID
        ptr[2] = (vehicle_id >> 8) & 0xFF;
        ptr[3] = vehicle_id & 0xFF;

        // 3. Sequence ID (32-bit)
        for(int i = 0; i < 4; i++) ptr[4 + i] = (sequence_id >> (24 - (i*8))) & 0xFF;

        // 4. Timestamp (64-bit)
        for(int i = 0; i < 8; i++) ptr[8 + i] = (timestamp >> (56 - (i*8))) & 0xFF;

        // 5. Physics Block (16-bit fields)
        ptr[16] = (rpm >> 8) & 0xFF;
        ptr[17] = rpm & 0xFF;

        ptr[18] = (speed >> 8) & 0xFF;
        ptr[19] = speed & 0xFF;

        ptr[20] = (jerk >> 8) & 0xFF;
        ptr[21] = jerk & 0xFF;

        // 6. Single Byte Fields (No shifting needed)
        ptr[22] = temp;
        ptr[23] = battery_level;
        ptr[24] = gear;
        ptr[25] = flags;
        ptr[26] = version;
        ptr[27] = cpu_load;

        // 7. CRC16
        ptr[28] = (crc16 >> 8) & 0xFF;
        ptr[29] = crc16 & 0xFF;

        // 8. Reserved (Zero out)
        ptr[30] = 0;
        ptr[31] = 0;
    }
};

#pragma pack(pop)

// Wire Format (32 Bytes per Packet)
static_assert(sizeof(Packet) == 32, "Packet size must be exactly 32 bytes");