#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <cstdint>
#include <functional>

#pragma comment (lib, "Ws2_32.lib")

const int KEEP_ALIVE_SEC = 20;
const uint8_t PACKET_CONNECT = 0x10;
const uint8_t PACKET_CONNACK = 0x20;
const uint8_t PACKET_PUBLISH = 0x30;
const uint8_t PACKET_PUBACK = 0x40;
const uint8_t PACKET_SUBSCRIBE = 0x82;
const uint8_t PACKET_SUBACK = 0x90;
const uint8_t PACKET_PINGREQ = 0xC0;
const uint8_t PACKET_PINGRESP = 0xD0;
const uint8_t PACKET_DISCONNECT = 0xE0;


class MqttForge {
    SOCKET sock;
    bool is_connected = false;
    uint16_t packet_id_counter = 1;
    std::chrono::steady_clock::time_point last_sent_time;

    using MsgCallback = std::function<void(std::string, const uint8_t*, int)>;
    MsgCallback m_on_msg;

public:
    MqttForge() : sock(INVALID_SOCKET){
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2,2), &wsaData);
    }
    ~MqttForge() {
        if(is_connected) Disconnect();
        WSACleanup();
    }

    void SetCallBack(MsgCallback cb){
        m_on_msg = cb;
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

    int DecodeLength(){
        int multiplier = 1;
        int value = 0;
        uint8_t encodedByte;
        while(true){
            if(!RecvExact(&encodedByte, 1)) return -1;
            value += (encodedByte & 127) * multiplier;
            multiplier *= 128;
            if(multiplier>128*128*128) return -1;
            if((encodedByte & 128) == 0) break;
        }

        return value;
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

    bool Subscribe(std::string topic){
        if(!is_connected) return false;

        // Packet ID
        uint16_t pid = packet_id_counter++;
        std::vector<uint8_t> payload;
        EncodeString(payload, topic);
        payload.push_back(0x01); // QoS 1

        // Header
        std::vector<uint8_t> packet;
        packet.push_back(PACKET_SUBSCRIBE);

        EncodeLength(packet, 2+payload.size());

        packet.push_back(pid>>8);
        packet.push_back(pid & 0xFF);
        packet.insert(packet.end(), payload.begin(), payload.end());

        if(!SendAll(packet)) return false;

        uint8_t header;
        if(!RecvExact(&header, 1)) return false;
        int len = DecodeLength();
        if(len<0) return false;

        // Read the remaining bytes to clear the buffer
        std::vector<uint8_t> body(len);
        if(!RecvExact(body.data(), len)) return false;

        if(header != PACKET_SUBACK){
            std::cout<<"[MQTT] Error: Expected SUBACK, got: " << (int) header << "\n" ;
            return false;
        }
        return true;
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

        // 1. Read
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        struct timeval tv = {0,0};

        int activity = select(0, &readfds, NULL, NULL, &tv);

        if(activity>0 && FD_ISSET(sock, &readfds)){
            uint8_t header;
            if(RecvExact(&header, 1)){
                int remaining_len = DecodeLength();
                if(remaining_len < 0) {
                    Disconnect();
                    return;
                }
                // Emptying the socket for the next message
                std::vector<uint8_t> buffer;
                if(remaining_len > 0) {
                    buffer.resize(remaining_len);
                    if(!RecvExact(buffer.data(), remaining_len)){
                        Disconnect();
                        return;
                    }
                }
                if((header & 0xF0) == PACKET_PUBLISH){
                    if(remaining_len>0){
                        uint16_t topic_len = (buffer[0] << 8)  | buffer[1];
                        if(topic_len + 2 <= remaining_len){
                            std::string topic((char*)&buffer[2], topic_len);
                            int offset = 2+topic_len;
                            if((header & 0x06) > 0) offset += 2;
                            if(m_on_msg)  m_on_msg(topic, &buffer[offset], remaining_len-offset);
                        }
                    }
                }
                else if(header == PACKET_PINGRESP){
                    std::cout << "[MQTT] Subscribed OK.\n";
                }
                else if(header == 0xD0){
                    std::cout << "[MQTT] Heartbeat received \n";
                }
            }
        }

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