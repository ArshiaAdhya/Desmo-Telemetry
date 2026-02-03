#include <iostream>
#include <vector>
#include <cassert>
#include <iomanip>
#include <cstddef>
#include "../include/packet.h"

// "Hardcore" Test Macro
#define ASSERT_EQ(val1, val2, msg) \
    if ((val1) != (val2)) { \
        std::cerr << "FAIL: " << msg << " (" << (val1) << " != " << (val2) << ")\n"; \
        std::exit(1); \
    } else { \
        std::cout << "PASS: " << msg << "\n"; \
    }

void test_packet_size() {
    ASSERT_EQ(sizeof(Packet), 32, "Packet size must be exactly 32 bytes");
}

void test_serialization_endianness() {
    Packet p{}; // Zero init
    p.magic = 0xD350;       
    p.vehicle_id = 0xAABB;  
    p.sequence_id = 0x11223344;
    p.timestamp = 0; // Avoid garbage values

    std::vector<uint8_t> buffer;
    p.serialize(buffer);

    // Verify MAGIC (0xD3, 0x50) - Big Endian
    ASSERT_EQ(buffer[0], 0xD3, "Byte 0 should be Magic High (0xD3)");
    ASSERT_EQ(buffer[1], 0x50, "Byte 1 should be Magic Low (0x50)");

    // Verify Sequence ID (0x11, 0x22, 0x33, 0x44)
    // Offset is 4 because Magic(2) + VehicleID(2) = 4
    ASSERT_EQ(buffer[4], 0x11, "Seq Byte 0 should be 0x11");
    ASSERT_EQ(buffer[7], 0x44, "Seq Byte 3 should be 0x44");
}

void test_alignment_offsets() {
    // Magic (0) + VehicleID (2) + Sequence (4) = Timestamp starts at 8
    ASSERT_EQ(offsetof(Packet, timestamp), 8, "Timestamp must start at byte 8");
    
    // ... + Timestamp (8) = Physics starts at 16
    ASSERT_EQ(offsetof(Packet, rpm), 16, "RPM must start at byte 16");
    
    // CRC is at the very end (before the 2 reserved bytes)
    // 32 total - 2 reserved - 2 CRC = 28
    ASSERT_EQ(offsetof(Packet, crc16), 28, "CRC16 must start at byte 28");
}

int main() {
    std::cout << "--- RUNNING UNIT TESTS ---\n";
    
    test_packet_size();
    test_alignment_offsets();
    test_serialization_endianness();

    std::cout << "--- ALL SYSTEMS OPERATIONAL ---\n";
    return 0;
}