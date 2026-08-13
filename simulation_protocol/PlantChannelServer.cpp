#include "PlantChannelServer.hpp"

#include "PlantCommandCodec.hpp"

#include <iostream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace
{

constexpr std::intptr_t kNoSocket = -1;
constexpr std::size_t kMaxRequestBytes = 8192;

#ifdef _WIN32
int closeNativeSocket(std::intptr_t handle)
{
    return closesocket(static_cast<SOCKET>(handle));
}

void setNonBlocking(std::intptr_t handle)
{
    u_long nonblock = 1;
    ioctlsocket(static_cast<SOCKET>(handle), FIONBIO, &nonblock);
}
#else
int closeNativeSocket(std::intptr_t handle)
{
    return close(static_cast<int>(handle));
}

void setNonBlocking(std::intptr_t handle)
{
    const int fd = static_cast<int>(handle);
    const int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, (flags < 0 ? 0 : flags) | O_NONBLOCK);
}
#endif

} // namespace

PlantChannelServer::~PlantChannelServer()
{
    stop();
}

bool PlantChannelServer::start(int port)
{
    const std::intptr_t sock = static_cast<std::intptr_t>(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
#ifdef _WIN32
    if (static_cast<SOCKET>(sock) == INVALID_SOCKET)
#else
    if (sock < 0)
#endif
    {
        std::cerr << "[PLANT] socket() failed" << std::endl;
        return false;
    }

    int yes = 1;
#ifdef _WIN32
    setsockopt(static_cast<SOCKET>(sock), SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&yes), sizeof(yes));
#else
    setsockopt(static_cast<int>(sock), SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<unsigned short>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

#ifdef _WIN32
    const SOCKET native = static_cast<SOCKET>(sock);
#else
    const int native = static_cast<int>(sock);
#endif

    if (bind(native, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
    {
        std::cerr << "[PLANT] bind() failed on port " << port << std::endl;
        closeNativeSocket(sock);
        return false;
    }
    if (listen(native, 4) != 0)
    {
        std::cerr << "[PLANT] listen() failed" << std::endl;
        closeNativeSocket(sock);
        return false;
    }

    setNonBlocking(sock);
    listen_sock_ = sock;
    return true;
}

void PlantChannelServer::stop()
{
    closeClient();
    if (listen_sock_ != kNoSocket)
    {
        closeNativeSocket(listen_sock_);
        listen_sock_ = kNoSocket;
    }
}

void PlantChannelServer::closeClient()
{
    if (client_ != kNoSocket)
    {
        closeNativeSocket(client_);
        client_ = kNoSocket;
    }
    rx_buffer_.clear();
}

bool PlantChannelServer::sendLine(const std::string& line)
{
    if (client_ == kNoSocket)
    {
        return false;
    }

    const std::string payload = line + "\n";
    std::size_t sent_total = 0;
    while (sent_total < payload.size())
    {
#ifdef _WIN32
        const int chunk = send(static_cast<SOCKET>(client_), payload.data() + sent_total,
                               static_cast<int>(payload.size() - sent_total), 0);
#else
        const ssize_t chunk = send(static_cast<int>(client_), payload.data() + sent_total,
                                   payload.size() - sent_total, 0);
#endif
        if (chunk <= 0)
        {
            return false;
        }
        sent_total += static_cast<std::size_t>(chunk);
    }
    return true;
}

void PlantChannelServer::handleLine(const std::string& line, simplant::SimPlant& plant, float x,
                                    float y, float z)
{
    const simplant::PlantCommand command = simplant::parsePlantCommand(line);
    if (!simplant::applyPlantCommand(plant, command))
    {
        sendLine(simplant::formatPlantErrorResponse("Unknown plant command"));
        return;
    }

    sendLine(simplant::formatPlantStateResponse(plant.state(x, y, z), x, y, z));
}

void PlantChannelServer::poll(simplant::SimPlant& plant, float x, float y, float z)
{
    if (listen_sock_ == kNoSocket)
    {
        return;
    }

    // Accept one pending connection; the channel serves a single client at a time.
#ifdef _WIN32
    const std::intptr_t incoming =
        static_cast<std::intptr_t>(accept(static_cast<SOCKET>(listen_sock_), nullptr, nullptr));
    const bool accepted = static_cast<SOCKET>(incoming) != INVALID_SOCKET;
#else
    const std::intptr_t incoming =
        static_cast<std::intptr_t>(accept(static_cast<int>(listen_sock_), nullptr, nullptr));
    const bool accepted = incoming >= 0;
#endif
    if (accepted)
    {
        closeClient();
        client_ = incoming;
        setNonBlocking(client_);
    }

    if (client_ == kNoSocket)
    {
        return;
    }

    char chunk[1024];
    for (;;)
    {
#ifdef _WIN32
        const int n = recv(static_cast<SOCKET>(client_), chunk, static_cast<int>(sizeof(chunk)), 0);
#else
        const ssize_t n = recv(static_cast<int>(client_), chunk, sizeof(chunk), 0);
#endif
        if (n == 0)
        {
            closeClient();
            return;
        }
        if (n < 0)
        {
            break;
        }

        rx_buffer_.append(chunk, static_cast<std::size_t>(n));
        if (rx_buffer_.size() > kMaxRequestBytes)
        {
            // Protect against garbage input on the simulation channel.
            rx_buffer_.clear();
            sendLine(simplant::formatPlantErrorResponse("Request too large"));
            continue;
        }

        for (;;)
        {
            const std::size_t newline = rx_buffer_.find('\n');
            if (newline == std::string::npos)
            {
                break;
            }

            std::string line = rx_buffer_.substr(0, newline);
            rx_buffer_.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            if (!line.empty())
            {
                handleLine(line, plant, x, y, z);
            }
        }
    }
}
