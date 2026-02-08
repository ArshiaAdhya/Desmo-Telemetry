#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <csignal>
#include <atomic>
#include <cmath>
#include <random>
#include "../include/vehicle.h"
#include "../include/packet.h"
#include "../include/mqtt_forge.h"

const double SIM_DT = 0.1;
std::atomic<bool> g_running(true);

enum DriverState {
    CITY_CRUISE,
    HIGHWAY_SPRINT,
    PANIC_STOP,
    IDLE,
    BATTERY_STRESS
};

uint16_t CalculateCRC(const uint8_t *data, size_t length){
    uint16_t crc = 0xFFFF;
    for(size_t i=0; i<length; i++){
        crc ^= (uint16_t)data[i] << 8;
        for(int j=0; j<8; j++){
            if(crc & 0x8000) crc = (crc<<1) ^ 0x1021;
            else crc <<= 1;
        }
    }
    return crc;
}

void signal_handler(int){
    g_running = false;
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signal_handler);
    uint16_t vehicle_id = 101;
    if(argc>1){
        try{
            vehicle_id = static_cast<uint16_t>(std::stoi(argv[1]));
        } catch(...){
            std::cerr<<"INVALID ID PROVIDED. Defaulting to 101\n";
        }
    }
    std::cout<<"----------------------DESMO FLEET: Vehicle: " << vehicle_id<< "--------------------\n";
    MqttForge uplink;
    Vehicle car(vehicle_id);

    Packet packet{};
    packet.magic = 0xD350; // Desmo System ;)
    packet.vehicle_id = 101;

    std::string client_id = "sim_client_" + std::to_string(vehicle_id);
    std::string topic = "fleet/"+std::to_string(vehicle_id)+"/telemetry";
    
    std::vector<uint8_t> buffer;
    buffer.reserve(32);
    uint32_t seq = 0;

    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<int> dice(0,99);

    DriverState current_state = CITY_CRUISE;
    int state_timer = 0;

    while(g_running){
        if(!uplink.Connect("127.0.0.1", 1883, client_id)){
            std::cout << "Connect Failed. Retrying";
            Sleep(2000);
            continue;
        }
        std::cout<<"Link Established. Telemetry System Active.\n";
        while(g_running){

            // Driver Logic
            if(state_timer++>100){
                state_timer = 0;
                int roll = dice(rng);
                if(roll<2){
                    current_state = PANIC_STOP;
                    std::cout<<"\n[!] PANIC!! SLAMMING BRAKES! \n";
                }
                else if(roll<22){
                    current_state = HIGHWAY_SPRINT;
                } 
                else if(roll<62){
                    current_state = CITY_CRUISE;
                }
                else if(roll>90){
                    current_state = IDLE;
                }
            }

            double throttle_input = 0.0;
            switch(current_state){
                case HIGHWAY_SPRINT:
                    throttle_input = 1.0;
                    break;
                case CITY_CRUISE:
                    throttle_input = (std::sin(seq*0.05)+1.0) / 2.0 * 0.6;
                    break;
                case PANIC_STOP:
                    throttle_input = -1.0;
                    break;
                case IDLE:
                    throttle_input = 0.0;
                    break;
                case BATTERY_STRESS:
                    throttle_input = 1.0;
                    break;
            }
            
            // Pedal to the metal
            car.SetThrottle(throttle_input);

            // Physics
            car.Tick(SIM_DT);
            car.Snapshot(packet, SIM_DT);

            // Metadata
            packet.sequence_id = seq++;
            auto now = std::chrono::system_clock::now();
            packet.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()
            ).count();

            // Serialization and checksum
            packet.crc16 = 0;
            packet.serialize(buffer);
            uint16_t checksum = CalculateCRC(buffer.data(), 28);
            buffer[28] = (checksum >> 8) & 0xFF;
            buffer[29] = (checksum & 0xFF);

            // Network Transmission
            // Publish to this with QOS1
            if(!uplink.Publish(topic, buffer, 1)){
                std::cerr << "LINK LOST (NO ACK). Reconnecting..\n";
                break;
            }

            // Maintenance
            uplink.Tick();

            if (seq % 10 == 0) {
                std::string status = "";
                if (packet.flags & Flags::ABS_ACTIVE) status = "[ABS ACTIVE]";
                else if (packet.flags & Flags::OVERHEAT) status = "[!!! OVERHEAT !!!]";
                else if (packet.flags & Flags::LOW_BATTERY) status = "[LOW BATTERY]";
                
                else if (current_state == HIGHWAY_SPRINT) status = "(SPRINT)";
                else if (current_state == BATTERY_STRESS) status = "(STRESS TEST)";
                else if (current_state == PANIC_STOP) status = "(BRAKING)";
                std::cout << "TX Seq:" << seq 
                        << " | RPM:" << packet.rpm 
                        << " | Spd:" << packet.speed << " km/h"
                        << " | " << status
                        << "   \r" << std::flush;
            }

            // Pacing
            Sleep(100);

        }

    }

    uplink.Disconnect();
    return 0;


}

