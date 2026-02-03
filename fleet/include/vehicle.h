#pragma once
#include <cstdint>
#include <random>
#include "packet.h"

class Vehicle{
public:
    Vehicle(uint16_t id);

    // Updates physics state by dt seconds
    void Tick(double dt_seconds);

    // Serializes internal state into Packet struct
    void Snapshot(Packet& packet);

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

    // Random noise generator for added realism
    std::mt19937 m_rng;
    std::normal_distribution<double> m_noise;
}