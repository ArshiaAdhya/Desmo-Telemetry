#pragma once
#include <cstdint>
#include <random>
#include "packet.h"

enum VehicleCommand : uint8_t {
    CMD_KILL = 0x01,
    CMD_LIMP = 0x02,
    CMD_NORMAL = 0x03
};

class Vehicle{
public:
    Vehicle(uint16_t id);

    // Updates physics state by dt seconds
    void Tick(double dt_seconds);

    // Serializes internal state into Packet struct
    void Snapshot(Packet& packet, double dt);

    // Calculate RPM on shifting up/down
    void CalculateRPM();

    // Calculate torque factor based on where we are in the power band
    double GetTorqueCurve(double rpm);

    // Set the throttle (multiplier for max_force)
    void SetThrottle(double throttle);

    // Called whenever a network packet arrives
    void OnCommand(uint8_t opcode);


private:
    uint16_t m_id;

    // Physics State
    double m_speed; // km/h
    double m_rpm; // 0-15000
    double m_temp; // Celsius
    double m_acceleration; // m/s^2
    double m_prev_accel;

    int m_gear;
    double m_target_speed;
    double m_throttle;
    double m_battery_level;
    bool m_remote_kill;
    bool m_limp_mode;

    // Random noise generator for added realism
    std::mt19937 m_rng;
    std::normal_distribution<double> m_noise;
};