// M13 binary M12 protocol TCP host (port 8766 default).
// M15: additionally serves the simulation plant channel (port 8767 default).
#include "FrameStreamBuffer.hpp"
#include "PlantChannelServer.hpp"
#include "SimProtocolBridge.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
static const socket_t kInvalidSocket = INVALID_SOCKET;
static int close_socket(socket_t s)
{
    return closesocket(s);
}
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
static const socket_t kInvalidSocket = -1;
static int close_socket(socket_t s)
{
    return close(s);
}
#endif

static bool sendFrames(socket_t client, const std::vector<simbridge::OutboundFrame>& frames)
{
    for (const simbridge::OutboundFrame& frame : frames)
    {
        std::size_t sent_total = 0;
        while (sent_total < frame.length)
        {
            const int chunk =
                send(client, reinterpret_cast<const char*>(frame.data + sent_total),
                     static_cast<int>(frame.length - sent_total), 0);
            if (chunk <= 0)
            {
                return false;
            }
            sent_total += static_cast<std::size_t>(chunk);
        }
    }
    return true;
}

static void readClientFrames(socket_t client, simbridge::FrameStreamBuffer& stream_buffer,
                             simbridge::SimProtocolBridge& bridge)
{
    char chunk[1024];
    for (;;)
    {
#ifdef _WIN32
        const int n = recv(client, chunk, static_cast<int>(sizeof(chunk)), 0);
#else
        const ssize_t n = recv(client, chunk, sizeof(chunk), MSG_DONTWAIT);
#endif
        if (n <= 0)
        {
            break;
        }

        stream_buffer.append(reinterpret_cast<const uint8_t*>(chunk), static_cast<std::size_t>(n));

        tecomm::DecodedFrame decoded{};
        tecomm::DecodeResult decode_result = tecomm::DecodeResult::NeedMoreData;
        while (stream_buffer.tryPopFrame(decoded, decode_result))
        {
            std::vector<simbridge::OutboundFrame> outbound;
            bridge.handleFrame(decoded, decode_result, outbound);
            if (!outbound.empty() && !sendFrames(client, outbound))
            {
                return;
            }
        }
    }
}

int main(int argc, char** argv)
{
    int port = 8766;
    if (argc >= 2)
    {
        port = std::atoi(argv[1]);
        if (port <= 0 || port > 65535)
        {
            std::cerr << "Invalid port; using 8766" << std::endl;
            port = 8766;
        }
    }

    // Simulation plant channel port (M15): argv[2] or PLANT_PORT, default 8767.
    int plant_port = 8767;
    const char* plant_port_env = std::getenv("PLANT_PORT");
    if (argc >= 3)
    {
        plant_port = std::atoi(argv[2]);
    }
    else if (plant_port_env != nullptr)
    {
        plant_port = std::atoi(plant_port_env);
    }
    if (plant_port <= 0 || plant_port > 65535)
    {
        std::cerr << "Invalid plant port; using 8767" << std::endl;
        plant_port = 8767;
    }

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        std::cerr << "WSAStartup failed" << std::endl;
        return 1;
    }
#endif

    std::cout << "simulation_protocol (M13 M12 binary TCP host)" << std::endl;
    std::cout << "Listening on 127.0.0.1:" << port << std::endl;

    const socket_t listen_sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock == kInvalidSocket)
    {
        std::cerr << "socket() failed" << std::endl;
        return 1;
    }

    int yes = 1;
#ifdef _WIN32
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
#else
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<unsigned short>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(listen_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
    {
        std::cerr << "bind() failed on port " << port << std::endl;
        close_socket(listen_sock);
        return 1;
    }
    if (listen(listen_sock, 4) != 0)
    {
        std::cerr << "listen() failed" << std::endl;
        close_socket(listen_sock);
        return 1;
    }

#ifdef _WIN32
    u_long nonblock = 1;
    ioctlsocket(listen_sock, FIONBIO, &nonblock);
#endif

    simbridge::SimProtocolBridge bridge;
    simbridge::FrameStreamBuffer stream_buffer;
    socket_t client = kInvalidSocket;

    // Sensor injection channel for the simulation UI (never carries M12 commands).
    PlantChannelServer plant_channel;
    if (plant_channel.start(plant_port))
    {
        std::cout << "Plant channel on 127.0.0.1:" << plant_port << std::endl;
    }
    else
    {
        std::cerr << "Plant channel unavailable (port " << plant_port << ")" << std::endl;
    }

    constexpr float kDt = 0.05f;
    using clock = std::chrono::steady_clock;
    auto next_tick = clock::now();

    for (;;)
    {
        sockaddr_in peer{};
#ifdef _WIN32
        int peer_len = sizeof(peer);
#else
        socklen_t peer_len = sizeof(peer);
#endif
        const socket_t incoming = accept(listen_sock, reinterpret_cast<sockaddr*>(&peer), &peer_len);
        if (incoming != kInvalidSocket)
        {
            if (client != kInvalidSocket)
            {
                close_socket(client);
            }
            client = incoming;
            stream_buffer.clear();
#ifdef _WIN32
            u_long nb = 1;
            ioctlsocket(client, FIONBIO, &nb);
#endif
            std::cout << "Client connected" << std::endl;

            std::vector<simbridge::OutboundFrame> hello;
            bridge.appendStatusPush(hello);
            if (!hello.empty())
            {
                sendFrames(client, hello);
            }
        }

        if (client != kInvalidSocket)
        {
            fd_set read_set;
            FD_ZERO(&read_set);
            FD_SET(client, &read_set);
            timeval tv{};
            tv.tv_sec = 0;
            tv.tv_usec = 0;
            const int ready = select(static_cast<int>(client) + 1, &read_set, nullptr, nullptr, &tv);
            if (ready > 0 && FD_ISSET(client, &read_set))
            {
#ifdef _WIN32
                char probe[1];
                const int peek = recv(client, probe, 1, MSG_PEEK);
                if (peek == 0)
                {
                    std::cout << "Client disconnected" << std::endl;
                    close_socket(client);
                    client = kInvalidSocket;
                    stream_buffer.clear();
                }
                else if (peek > 0)
                {
                    readClientFrames(client, stream_buffer, bridge);
                }
#else
                readClientFrames(client, stream_buffer, bridge);
#endif
            }
        }

        // Sensor configuration/injection from the simulation UI (no motion here).
        plant_channel.poll(bridge.plant(), bridge.machine().x(), bridge.machine().y(),
                           bridge.machine().z());

        const auto now = clock::now();
        if (now >= next_tick)
        {
            bridge.applyHostInputs();
            bridge.tick(kDt);

            if (client != kInvalidSocket)
            {
                std::vector<simbridge::OutboundFrame> status_push;
                bridge.appendForcePathChunks(status_push);
                bridge.appendStatusPush(status_push);
                if (!status_push.empty() && !sendFrames(client, status_push))
                {
                    std::cout << "Client send failed; disconnecting" << std::endl;
                    close_socket(client);
                    client = kInvalidSocket;
                    stream_buffer.clear();
                }
            }

            next_tick += std::chrono::duration_cast<clock::duration>(std::chrono::duration<float>(kDt));
            if (next_tick < now)
            {
                next_tick = now;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
