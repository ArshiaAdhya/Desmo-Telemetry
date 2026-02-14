#include "../include/vehicle.h"
#include <algorithm>
#include <cmath>
#include <iostream>

template <typename T>
inline T clamp(T v, T lo, T hi)
{
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

Vehicle::Vehicle(uint16_t id) 
        : m_id(id), 
        m_noise(0.0,2.5), 
        m_battery_level(100.0),
        m_remote_kill(false),
        m_limp_mode(false) {
    m_rng.seed(id);
    m_speed = 0.0;
    m_rpm = 800;
    m_temp = 25.0;
    m_gear = 1;

    m_target_speed = 110.0+(id%50);
    m_acceleration = 0.0;
    m_prev_accel = 0.0;
    m_throttle = 0.0;
}

double Vehicle::GetTorqueCurve(double rpm){
    // peak torque at 4500 RPM

    double deviation = (rpm-4500)/4500;
    double curve_factor = 1.0 - (deviation*deviation);

    return clamp(curve_factor, 0.3, 1.0);
}

void Vehicle::CalculateRPM(){
    if(m_remote_kill){
        m_rpm = 0.0;
        return;
    }
    double gear_ratio = 4.8 - (m_gear*0.65);
    if(gear_ratio<0.8) gear_ratio = 0.8;

    // RPM = Speed+Ratio*FinalDrive
    m_rpm = m_speed*gear_ratio*25.0;

    // Engine Idle Floor
    if (m_rpm<800.0) m_rpm = 800.0;

    // Safety Redline Cap
    if(m_rpm>16000.0) m_rpm = 16000.0;
}

void Vehicle::SetThrottle(double throttle){
    throttle = clamp(throttle, -1.0, 1.0);
    m_throttle = throttle;
}

void Vehicle::OnCommand(uint8_t opcode){
    switch (opcode) {
        case CMD_KILL:
            m_remote_kill = true;
            std::cout<<"[CMD] REMOTE KILL SWITCH ACTIVE!!";
            break;
        case CMD_LIMP:
            m_limp_mode = true;
            std::cout<<"[CMD] LIMP MODE ACTIVE!!";
            break;
        case CMD_NORMAL:
            m_remote_kill = false;
            m_limp_mode = false;
            std::cout<<"[CMD] REGULAR MODE. \n";
            break;
        default:
            std::cout<<"[CMD] Unknown OpCode: " << (int)opcode << "\n";
            break;
    }
}

void Vehicle::Tick(double dt){
    // 1. CONTINUOUS THROTTLE (Proportional Control)
    // Error = target - current
    double speed_error = m_target_speed - m_speed;

    double internal_demand = clamp(speed_error*0.1, 0.0, 1.0);
    double final_throttle = (m_throttle>0) ? m_throttle : internal_demand;

    // Intervention logic
    if(m_remote_kill){
        final_throttle = -1.0;
        m_rpm = 0;
    } else if(m_limp_mode){
        if(m_speed>40.0) final_throttle = -0.5;
        else if(final_throttle>0.3) final_throttle = 0.3;
    }

    // 2. Engine force
    double max_torque = 100.0;
    double torque_curve = GetTorqueCurve(m_rpm);

    // Final force (PID)
    double force_engine = final_throttle * torque_curve * max_torque;

    // 3. RESISTANCE
    double force_friction = (m_speed>0) ? 5.0 : 0.0;
    double force_drag = 0.0035*m_speed*m_speed;

    // 4. INTEGRATION
    double net_force = force_engine - force_friction - force_drag;

    // Engine Braking
    if(final_throttle<-0.1) net_force -= (std::abs(final_throttle))*15.0;
    if (final_throttle<0.05 && m_speed>0) net_force -= 2.0;

    m_prev_accel = m_acceleration;
    m_acceleration = net_force;

    m_speed += (m_acceleration*dt);
    if(m_speed<0) m_speed = 0;

    CalculateRPM();

    bool shifted = false;
    if(m_rpm>7500 && m_gear<6) {
        m_gear++; shifted = true;
    }
    else if(m_rpm < 2500 && m_gear>1) {
        m_gear--; shifted = true;
    }

    // Recalculate RPM after shift
    if(shifted) CalculateRPM();

    // Thermodynamics
    double heat_in = (m_rpm/3000.0)*15.0*dt;
    double heat_out = (m_temp-25.0)*0.2*dt;
    m_temp += (heat_in - heat_out);
    m_temp = clamp(m_temp,25.0,150.0);

    if(m_speed>0) {
        m_battery_level -= (0.05 * dt);
    }
    if(m_battery_level<0) m_battery_level = 0;

}

void Vehicle::Snapshot(Packet &p, double dt){
    p.vehicle_id = m_id;
    p.version = 1;
    double noisy_rpm = m_rpm + m_noise(m_rng);
    p.rpm = static_cast<uint16_t>(clamp(noisy_rpm, 0.0, 16000.0));
    p.speed = static_cast<uint16_t>(m_speed);
    p.gear = static_cast<uint8_t>(m_gear);
    p.temp = static_cast<uint8_t>(m_temp);
    p.battery_level = static_cast<uint8_t>(m_battery_level);
    if(dt>0.0001){
        double jerk_per_second = (m_acceleration - m_prev_accel)/0.1;
        p.jerk = static_cast<int16_t>(jerk_per_second * 100.0);
    }
    else p.jerk = 0;
    p.flags = 0;
    if(p.temp>115) p.flags |= Flags::OVERHEAT;
    if(p.battery_level<20) p.flags |= Flags::LOW_BATTERY;
    if(m_acceleration<-5.0) p.flags |= Flags::ABS_ACTIVE;

    if(m_remote_kill) p.flags |= Flags::REMOTE_KILL;

    p.cpu_load = 10 + (rand()%30);

    p.reserved[0] = 0;
    p.reserved[1] = 1;

}