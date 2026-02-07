#include <iostream>
#include <vector>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iomanip>

#include "../include/vehicle.h"
#include "../include/packet.h"

// --- UTILITIES ---
void print_pass(const std::string& name) {
    std::cout << "[PASS] " << name << "\n";
}

void print_fail(const std::string& name, const std::string& reason) {
    std::cerr << "[FAIL] " << name << ": " << reason << "\n";
    exit(1);
}

// --- TEST CASES ---

void Test_PacketSerialization() {
    Packet p;
    p.magic = 0xD350;
    p.vehicle_id = 101;
    p.sequence_id = 1;
    p.timestamp = 1700000000000;
    p.rpm = 4000;
    p.speed = 100;
    p.jerk = -150;
    p.temp = 90;
    p.battery_level = 80;
    p.gear = 3;
    p.flags = Flags::ABS_ACTIVE; // 0x08
    p.version = 1;
    p.cpu_load = 15;
    p.crc16 = 0xABCD; // Dummy CRC

    std::vector<uint8_t> buffer;
    p.serialize(buffer);

    // Verify Size
    if (buffer.size() != 32) print_fail("Packet Size", "Expected 32 bytes");

    // Verify Magic (Big Endian)
    if (buffer[0] != 0xD3 || buffer[1] != 0x50) print_fail("Magic Header", "Incorrect bytes");

    // Verify Flags (Byte 25)
    if (buffer[25] != 0x08) print_fail("Flags Packing", "Expected 0x08 for ABS");

    // Verify CRC Placement (Last real bytes before padding)
    // CRC is at index 28, 29
    if (buffer[28] != 0xAB || buffer[29] != 0xCD) print_fail("CRC Placement", "Incorrect position");

    print_pass("Packet Serialization (Binary Layout)");
}

void Test_Physics_RPM() {
    Vehicle car(101);
    
    // Test 1: Idle RPM
    car.Tick(0.1); 
    Packet p; 
    car.Snapshot(p, 0.1);
    
    if (p.rpm < 800) print_fail("Idle RPM", "RPM dropped below 800");

    // Test 2: Acceleration Logic
    car.SetThrottle(1.0);
    // Simulate 2 seconds of flooring it
    for(int i=0; i<20; i++) car.Tick(0.1); 
    
    car.Snapshot(p, 0.1);
    if (p.speed <= 0) print_fail("Acceleration", "Speed did not increase with throttle");
    if (p.rpm <= 800) print_fail("RPM Response", "RPM did not rise with speed");

    print_pass("Physics: RPM & Acceleration");
}

void Test_Flags_ABS() {
    Vehicle car(102);
    
    // 1. Get up to speed (High speed needed for panic brake)
    car.SetThrottle(1.0);
    for(int i=0; i<300; i++) car.Tick(0.1); // Run for 30s
    
    Packet p;
    car.Snapshot(p, 0.1);
    // Ensure we are actually moving fast enough to trigger ABS
    if (p.speed < 80) {
        std::cout << "Warning: Car too slow (" << p.speed << "km/h) for ABS test. Extending run.\n";
        for(int i=0; i<200; i++) car.Tick(0.1); 
    }

    // 2. PANIC STOP!
    car.SetThrottle(-1.0); 
    car.Tick(0.1);
    car.Snapshot(p, 0.1);

    // Check Flag
    if (!(p.flags & Flags::ABS_ACTIVE)) {
        std::cout << "Debug: Accel is " << (p.jerk/100.0) << " m/s^2\n";
        print_fail("ABS Logic", "ABS Flag not triggered on panic stop");
    }

    print_pass("Flags: ABS Trigger");
}

void Test_Flags_Overheat() {
    Vehicle car(103);
    
    // Pin throttle for a long time to generate heat
    car.SetThrottle(1.0);
    
    bool triggered = false;
    // Simulate up to 600 seconds
    for(int i=0; i<6000; i++) { 
        car.Tick(0.1);
        Packet p;
        car.Snapshot(p, 0.1);
        
        if (p.flags & Flags::OVERHEAT) {
            triggered = true;
            break;
        }
    }

    if (!triggered) print_fail("Thermodynamics", "Engine failed to overheat under max load");
    print_pass("Flags: Overheat Threshold");
}

void Test_Battery_Drain() {
    Vehicle car(104);
    Packet p_start, p_end;
    
    car.Snapshot(p_start, 0.1);
    
    // Drive for 100 seconds
    car.SetThrottle(0.5);
    for(int i=0; i<1000; i++) car.Tick(0.1);
    
    car.Snapshot(p_end, 0.1);

    if (p_end.battery_level >= p_start.battery_level) {
        print_fail("Battery Logic", "Battery did not drain while driving");
    }
    
    print_pass("Physics: Battery Drain");
}

int main() {
    std::cout << "--- RUNNING UNIT TESTS ---\n";
    
    Test_PacketSerialization();
    Test_Physics_RPM();
    Test_Flags_ABS();
    Test_Battery_Drain();
    Test_Flags_Overheat();
    
    std::cout << "--- ALL TESTS PASSED ---\n";
    return 0;
}