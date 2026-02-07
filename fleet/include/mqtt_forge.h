#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <cstdint>

#pragma comment (lib, "Ws2_32.lib")

const int KEEP_ALIVE_SEC = 20;
const uint8_t PACKET_CONNECT = 0x10;
const uint8_t PACKET_CONNACK = 0x20;
const uint8_t PACKET_PUBLISH = 0x30;
const uint8_t PACKET_PUBACK = 0x40;
const uint8_t PACKET_PINGREQ = 0xC0;
const uint8_t PACKET_DISCONNECT = 0xE0;


class MqttForge {
    SOCKET sock;
    bool is_connected = false;
    uint16_t packet_id_counter = 1;
    std::chrono::steady_clock::time_point last_sent_time;

public:
    MqttForge() : sock(INVALID_SOCKET){
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2,2), &wsaData);
    }
    ~MqttForge() {
        if(is_connected) Disconnect();
        WSACleanup();
    }

    bool SendAll(const std::vector<uint8_t> &data){
        if(sock==INVALID_SOCKET) return false;
        size_t total_sent = 0;
        while(total_sent<data.size()){
            int n = send(sock, (const  char*) data.data() + total_sent, (int)(data.size()-total_sent), 0);
            if(n<=0) return false;
            total_sent += n;
        }
        last_sent_time = std::chrono::steady_clock::now();
        return true;
    }

    bool RecvExact(uint8_t *buffer, int len){
        int total_read = 0;
        while(total_read<len){
            int n = recv(sock, (char *)buffer+total_read, len-total_read,0);
            if(n<=0) return false;
            total_read += n;
        }
        return true;
    }

    void EncodeLength(std::vector<uint8_t> &buffer, int length){
        while(length>=128){
            buffer.push_back((length%128) | 0x80);
            length /= 128;
        }

        buffer.push_back(static_cast<uint8_t> (length));
    }

    void EncodeString(std::vector<uint8_t> &buffer, const std::string &str){
        uint16_t len = static_cast<uint16_t> (str.length());
        buffer.push_back(len>>8); // Push high byte of the length
        buffer.push_back(len&0xFF); // Push the low byte
        buffer.insert(buffer.end(), str.begin(), str.end());
    }

    bool Connect(std::string ip, int port, std::string client_id){
        // Socket init and timeout
        if(sock!=INVALID_SOCKET) closesocket(sock);

        sock = socket(AF_INET, SOCK_STREAM, 0);
        if(sock==INVALID_SOCKET) return false;

        DWORD timeout = 2000;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

        struct sockaddr_in server;
        server.sin_family = AF_INET;
        server.sin_addr.s_addr = inet_addr(ip.c_str());
        server.sin_port = htons(port);

        if (connect(sock, (struct sockaddr*)&server, sizeof(server)) < 0) return false;

        std::vector<uint8_t> var_header = {0x00, 0x04, 'M', 'Q', 'T', 'T', 0x04, 0x02};
        var_header.push_back(KEEP_ALIVE_SEC >> 8);
        var_header.push_back(KEEP_ALIVE_SEC & 0xFF);

        std::vector<uint8_t> payload;
        EncodeString(payload, client_id);

        std::vector<uint8_t> packet;
        packet.push_back(PACKET_CONNECT);
        EncodeLength(packet, var_header.size()+payload.size());
        packet.insert(packet.end(), var_header.begin(), var_header.end());
        packet.insert(packet.end(), payload.begin(), payload.end());

        if(!SendAll(packet)) return false;
        uint8_t ack[4];
        if(!RecvExact(ack, 4)) return false;
        if(ack[0]==PACKET_CONNACK && ack[3]==0x00) { // 0x00 means connection accepted
            is_connected = true;
            return true;
        }
        return false;
    }

    bool Publish(std::string topic, const std::vector<uint8_t> &payload, int qos = 1){
        if(!is_connected) return false;
        std::vector<uint8_t> var_header;
        EncodeString(var_header, topic);
        uint16_t pid = 0;
        if(qos>0){
            pid = packet_id_counter++;
            if(pid==0) pid=1;
            var_header.push_back(pid>>8);
            var_header.push_back(pid&0xFF);
        }
        std::vector<uint8_t> packet;
        uint8_t type = PACKET_PUBLISH;
        if(qos==1) type |= 0x02;
        packet.push_back(type);
        EncodeLength(packet, var_header.size()+payload.size());
        packet.insert(packet.end(), var_header.begin(), var_header.end());
        packet.insert(packet.end(), payload.begin(), payload.end());

        if (!SendAll(packet)){ 
            is_connected = false; 
            return false;
        }

        if (qos==1){
            uint8_t ack[4];
            if(!RecvExact(ack,4)) {
                is_connected = false;
                return false;
            }
            if(ack[0]==PACKET_PUBACK) {
                uint16_t ack_pid = (ack[2]<<8) | ack[3]; // ID sent back by the broker.
                return (ack_pid==pid);
            }
            return false;
        }
        return true;
    }

    void Tick() {
        if(!is_connected) return;
        auto now = std::chrono::steady_clock::now();
        // Send a Ping if nothing's been sent for 15s
        if(std::chrono::duration_cast<std::chrono::seconds>(now - last_sent_time).count() >= 15){
            std::vector<uint8_t> ping = {PACKET_PINGREQ, 0x00}; // 0xC0 0x00 fixed ping packet
            SendAll(ping);
        }
    }

    void Disconnect() {
        if(!is_connected) return;
        std::vector<uint8_t> disc = {PACKET_DISCONNECT,0x00};
        SendAll(disc);
        closesocket(sock);
        is_connected = false;
    }
};