#pragma once

#include "SimPlant.hpp"

#include <cstdint>
#include <string>

// M15 simulation plant channel: line-delimited JSON over TCP on loopback.
//
// This channel exists only for the PC simulation (virtual sensors) and is
// intentionally separate from the M12 controller protocol. It carries no
// machine commands; it configures the virtual outside world.
class PlantChannelServer
{
public:
    ~PlantChannelServer();

    // Bind and listen on 127.0.0.1:port. Returns false if the port is taken.
    bool start(int port);

    // Non-blocking service step: accept, read requests, answer with plant state.
    // x/y/z = Core raw TCP. offset_* = Core referenceOffset (display; not a sensor).
    void poll(simplant::SimPlant& plant, float x, float y, float z, float offset_x, float offset_y,
              float offset_z);

    void stop();

private:
    void closeClient();
    void handleLine(const std::string& line, simplant::SimPlant& plant, float x, float y, float z,
                    float offset_x, float offset_y, float offset_z);
    bool sendLine(const std::string& line);

    // Platform socket handles kept as intptr_t so this header stays socket-free.
    std::intptr_t listen_sock_ = -1;
    std::intptr_t client_ = -1;
    std::string rx_buffer_;
};
