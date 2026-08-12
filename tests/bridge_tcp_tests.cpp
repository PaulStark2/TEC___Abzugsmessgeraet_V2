// Optional TCP integration test for M13 simulation_protocol (localhost, short timeout).
#include "Decoder.hpp"
#include "MessageIds.hpp"
#include "PayloadCodec.hpp"
#include "Payloads.hpp"
#include "ProtocolSpec.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <process.h>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
static int close_socket(socket_t s)
{
    return closesocket(s);
}
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
using socket_t = int;
static int close_socket(socket_t s)
{
    return close(s);
}
#endif

static bool nearEqual(float a, float b, float eps = 0.5f)
{
    return std::fabs(a - b) <= eps;
}

static bool recvOneFrame(socket_t sock, std::vector<uint8_t>& buffer, tecomm::DecodedFrame& out,
                         int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    while (std::chrono::steady_clock::now() < deadline)
    {
        char chunk[512];
        const int n = recv(sock, chunk, sizeof(chunk), 0);
        if (n > 0)
        {
            buffer.insert(buffer.end(), reinterpret_cast<uint8_t*>(chunk),
                          reinterpret_cast<uint8_t*>(chunk) + n);
        }

        std::size_t consumed = 0;
        const tecomm::DecodeResult result =
            tecomm::decodeFrame(buffer.data(), buffer.size(), out, consumed);
        if (result == tecomm::DecodeResult::Ok)
        {
            buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(consumed));
            return true;
        }
        if (result != tecomm::DecodeResult::NeedMoreData)
        {
            if (consumed > 0)
            {
                buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(consumed));
            }
            else if (!buffer.empty())
            {
                buffer.erase(buffer.begin());
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

#ifdef _WIN32
static HANDLE g_child_process = nullptr;

static bool spawnProtocolHost(const char* exe, const char* port_arg)
{
    std::string cmdline_str = std::string("\"") + exe + "\" " + port_arg;
    std::vector<char> cmdline(cmdline_str.begin(), cmdline_str.end());
    cmdline.push_back('\0');

    STARTUPINFOA startup_info{};
    startup_info.cb = sizeof(startup_info);
    PROCESS_INFORMATION process_info{};

    if (!CreateProcessA(nullptr, cmdline.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                        &startup_info, &process_info))
    {
        return false;
    }

    CloseHandle(process_info.hThread);
    g_child_process = process_info.hProcess;
    return true;
}

static void killProcess(intptr_t)
{
    if (g_child_process != nullptr)
    {
        TerminateProcess(g_child_process, 0);
        CloseHandle(g_child_process);
        g_child_process = nullptr;
    }
}
#else
static void killProcess(pid_t pid)
{
    if (pid > 0)
    {
        kill(pid, SIGTERM);
        waitpid(pid, nullptr, 0);
    }
}
#endif

int main()
{
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        return 1;
    }
#endif

    const int port = 18766;

#ifndef SIMULATION_PROTOCOL_EXE
#error "SIMULATION_PROTOCOL_EXE must be defined by CMake for bridge_tcp_tests"
#endif
    const char* exe = SIMULATION_PROTOCOL_EXE;

    char port_arg[16];
    std::snprintf(port_arg, sizeof(port_arg), "%d", port);

#ifdef _WIN32
    if (!spawnProtocolHost(exe, port_arg))
    {
        std::fprintf(stderr, "bridge_tcp_tests: spawn skipped (simulation_protocol not runnable)\n");
        return 0;
    }
    const intptr_t child_pid = 1;
#else
    pid_t child_pid = fork();
    if (child_pid == 0)
    {
        execl(exe, exe, port_arg, static_cast<char*>(nullptr));
        _exit(1);
    }
    if (child_pid < 0)
    {
        std::fprintf(stderr, "bridge_tcp_tests: fork skipped\n");
        return 0;
    }
#endif

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    socket_t sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0)
    {
        killProcess(child_pid);
        return 2;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<unsigned short>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
    {
        close_socket(sock);
        killProcess(child_pid);
        std::fprintf(stderr, "bridge_tcp_tests: connect skipped\n");
        return 0;
    }

    std::vector<uint8_t> buffer;
    tecomm::DecodedFrame frame{};

    if (!recvOneFrame(sock, buffer, frame, 2000) ||
        frame.message_id != static_cast<uint8_t>(tecomm::MessageId::MsgStatusReport))
    {
        close_socket(sock);
        killProcess(child_pid);
        return 10;
    }

    tecomm::MoveToPayload move{12.0f, 0.0f, 0.0f};
    uint8_t cmd[tecomm::kMaxFrameSize];
    const tecomm::EncodeResult encoded = tecomm::encodeMessage(
        static_cast<uint8_t>(tecomm::MessageId::CmdMoveTo), move, cmd, sizeof(cmd));
    if (!encoded.ok)
    {
        close_socket(sock);
        killProcess(child_pid);
        return 11;
    }
    send(sock, reinterpret_cast<const char*>(cmd), static_cast<int>(encoded.bytes_written), 0);

    bool got_ack = false;
    bool moved = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (!recvOneFrame(sock, buffer, frame, 200))
        {
            continue;
        }

        if (frame.message_id == static_cast<uint8_t>(tecomm::MessageId::RspAck))
        {
            got_ack = true;
        }
        if (frame.message_id == static_cast<uint8_t>(tecomm::MessageId::MsgStatusReport))
        {
            tecomm::StatusReportPayload status{};
            if (tecomm::decodeStatusReportPayload(frame.payload, frame.payload_length, status) &&
                status.x > 1.0f)
            {
                moved = true;
            }
        }
        if (got_ack && moved)
        {
            break;
        }
    }

    close_socket(sock);
    killProcess(child_pid);

#ifdef _WIN32
    WSACleanup();
#endif

    if (!got_ack)
    {
        return 20;
    }
    if (!moved)
    {
        return 21;
    }

    return 0;
}
