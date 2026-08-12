// M8.5 interactive host: long-running Machine tick loop over temporary TCP/JSON.
// This is a PRE-PROTOCOL simulation transport only — not Core M12/M13 communication/.
#include "HostInputs.hpp"
#include "Machine.hpp"
#include "Types.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
static int close_socket(socket_t s) { return closesocket(s); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
static const socket_t kInvalidSocket = -1;
static int close_socket(socket_t s) { return close(s); }
#endif

// Convert MachineState to the G3-aligned JSON label.
static const char* toString(MachineState s)
{
    switch (s)
    {
    case MachineState::Idle:
        return "Idle";
    case MachineState::Active:
        return "Active";
    case MachineState::Paused:
        return "Paused";
    case MachineState::Stopped:
        return "Stopped";
    case MachineState::Fault:
        return "Fault";
    default:
        return "Unknown";
    }
}

// Convert MotionState to the G3-aligned JSON label.
static const char* toString(MotionState s)
{
    switch (s)
    {
    case MotionState::Idle:
        return "Idle";
    case MotionState::Moving:
        return "Moving";
    case MotionState::AtTarget:
        return "AtTarget";
    default:
        return "Unknown";
    }
}

// M9 deterministic 3D force stub [N]: fx=kx*x, fy=ky*y, fz=kz*z (not a physical sensor model).
static ForceVector demoForceFromPose(float x, float y, float z)
{
    constexpr float kx = 1.0f; // N per machine unit along +X
    constexpr float ky = 1.0f; // N per machine unit along +Y
    constexpr float kz = 1.0f; // N per machine unit along +Z
    return ForceVector{kx * x, ky * y, kz * z};
}

// Build one newline-terminated status JSON line from Machine.
static std::string buildStatusLine(const Machine& machine)
{
    char buf[256];
    std::snprintf(
        buf,
        sizeof(buf),
        "{\"type\":\"status\",\"machineState\":\"%s\",\"motionState\":\"%s\",\"x\":%.6g,\"y\":%.6g,\"z\":%.6g}\n",
        toString(machine.machineState()),
        toString(machine.motionState()),
        static_cast<double>(machine.x()),
        static_cast<double>(machine.y()),
        static_cast<double>(machine.z()));
    return std::string(buf);
}

// Extract a JSON string field value after "key":"
static bool extractStringField(const std::string& json, const char* key, std::string& out)
{
    const std::string pattern = std::string("\"") + key + "\"";
    const std::size_t key_pos = json.find(pattern);
    if (key_pos == std::string::npos)
    {
        return false;
    }
    const std::size_t colon = json.find(':', key_pos + pattern.size());
    if (colon == std::string::npos)
    {
        return false;
    }
    const std::size_t q1 = json.find('"', colon + 1);
    if (q1 == std::string::npos)
    {
        return false;
    }
    const std::size_t q2 = json.find('"', q1 + 1);
    if (q2 == std::string::npos)
    {
        return false;
    }
    out = json.substr(q1 + 1, q2 - q1 - 1);
    return true;
}

// Extract a JSON number field after "key":
static bool extractNumberField(const std::string& json, const char* key, float& out)
{
    const std::string pattern = std::string("\"") + key + "\"";
    const std::size_t key_pos = json.find(pattern);
    if (key_pos == std::string::npos)
    {
        return false;
    }
    const std::size_t colon = json.find(':', key_pos + pattern.size());
    if (colon == std::string::npos)
    {
        return false;
    }
    std::size_t i = colon + 1;
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t'))
    {
        ++i;
    }
    char* end = nullptr;
    const float value = std::strtof(json.c_str() + i, &end);
    if (end == json.c_str() + i)
    {
        return false;
    }
    out = value;
    return true;
}

// Apply one command JSON line to Machine (Machine API semantics only).
static void applyCommand(Machine& machine, const std::string& line)
{
    std::string command;
    if (!extractStringField(line, "command", command))
    {
        return;
    }

    if (command == "moveTo")
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        if (!extractNumberField(line, "x", x) || !extractNumberField(line, "y", y) ||
            !extractNumberField(line, "z", z))
        {
            return;
        }
        machine.moveTo(x, y, z);
        return;
    }
    if (command == "pause")
    {
        machine.pause();
        return;
    }
    if (command == "resume")
    {
        machine.resume();
        return;
    }
    if (command == "stop")
    {
        machine.stop();
        return;
    }
}

// Drain available bytes from a client socket into a line buffer; handle full lines.
static void readClientCommands(socket_t client, std::string& buffer, Machine& machine)
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
        buffer.append(chunk, static_cast<std::size_t>(n));
        for (;;)
        {
            const std::size_t nl = buffer.find('\n');
            if (nl == std::string::npos)
            {
                break;
            }
            std::string line = buffer.substr(0, nl);
            buffer.erase(0, nl + 1);
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            if (!line.empty())
            {
                applyCommand(machine, line);
            }
        }
    }
}

int main(int argc, char** argv)
{
    int port = 8765;
    if (argc >= 2)
    {
        port = std::atoi(argv[1]);
        if (port <= 0 || port > 65535)
        {
            std::cerr << "Invalid port; using 8765" << std::endl;
            port = 8765;
        }
    }

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        std::cerr << "WSAStartup failed" << std::endl;
        return 1;
    }
#endif

    std::cout << "simulation_interactive (M8.5 pre-protocol TCP/JSON host)" << std::endl;
    std::cout << "NOT the future M12/M13 communication protocol." << std::endl;
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
#else
    // accept non-blocking via select
#endif

    Machine machine;
    socket_t client = kInvalidSocket;
    std::string client_buffer;

    constexpr float kDt = 0.05f;
    using clock = std::chrono::steady_clock;
    auto next_tick = clock::now();

    for (;;)
    {
        // Accept a single interactive client (replace previous if a new one connects).
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
            client_buffer.clear();
#ifdef _WIN32
            u_long nb = 1;
            ioctlsocket(client, FIONBIO, &nb);
#else
            // keep blocking recv short via select below
#endif
            std::cout << "Client connected" << std::endl;
            const std::string hello = buildStatusLine(machine);
            send(client, hello.c_str(), static_cast<int>(hello.size()), 0);
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
                    client_buffer.clear();
                }
                else if (peek > 0)
                {
                    readClientCommands(client, client_buffer, machine);
                }
#else
                readClientCommands(client, client_buffer, machine);
#endif
            }
        }

        // Fixed-dt Machine tick (motion only in Core).
        const auto now = clock::now();
        if (now >= next_tick)
        {
            HostInputs inputs{};
            inputs.force = demoForceFromPose(machine.x(), machine.y(), machine.z());
            inputs.e_stop = false;
            machine.setHostInputs(inputs);
            machine.tick(kDt);

            if (client != kInvalidSocket)
            {
                const std::string status = buildStatusLine(machine);
                const int sent = send(client, status.c_str(), static_cast<int>(status.size()), 0);
                if (sent <= 0)
                {
                    std::cout << "Client send failed; disconnecting" << std::endl;
                    close_socket(client);
                    client = kInvalidSocket;
                    client_buffer.clear();
                }
            }

            next_tick += std::chrono::duration_cast<clock::duration>(
                std::chrono::duration<float>(kDt));
            if (next_tick < now)
            {
                next_tick = now;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Unreachable — long-running host process.
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
