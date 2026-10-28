#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <iostream>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    using socket_handle_t = SOCKET;
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <fcntl.h>
    #include <unistd.h>
    #include <errno.h>
    using socket_handle_t = int;
    constexpr socket_handle_t INVALID_SOCKET = -1;
    constexpr int SOCKET_ERROR = -1;
#endif

namespace DeltaCompression {

// Cross-platform raw non-blocking UDP Socket wrapper
class UdpSocket {
public:
    UdpSocket() noexcept : m_socket(INVALID_SOCKET) {}

    ~UdpSocket() {
        Close();
    }

    // Initialize OS socket subsystem (e.g. Winsock WSAStartup)
    static bool GlobalInit() noexcept {
#ifdef _WIN32
        WSADATA wsaData;
        return (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0);
#else
        return true;
#endif
    }

    static void GlobalShutdown() noexcept {
#ifdef _WIN32
        WSACleanup();
#endif
    }

    // Open a non-blocking UDP socket and optionally bind to a local port
    bool Open(uint16_t port = 0) noexcept {
        Close();

        m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#ifdef _WIN32
        if (m_socket == INVALID_SOCKET) return false;
#else
        if (m_socket < 0) {
            m_socket = INVALID_SOCKET;
            return false;
        }
#endif

        // Set socket to non-blocking mode
#ifdef _WIN32
        u_long nonBlocking = 1;
        if (ioctlsocket(m_socket, FIONBIO, &nonBlocking) != 0) {
            Close();
            return false;
        }
#else
        int flags = fcntl(m_socket, F_GETFL, 0);
        if (flags == -1 || fcntl(m_socket, F_SETFL, flags | O_NONBLOCK) == -1) {
            Close();
            return false;
        }
#endif

        // Bind if a specific local port was provided
        if (port > 0) {
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = INADDR_ANY;
            address.sin_port = htons(port);

            if (bind(m_socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
                Close();
                return false;
            }
        }

        return true;
    }

    void Close() noexcept {
        if (m_socket != INVALID_SOCKET) {
#ifdef _WIN32
            closesocket(m_socket);
#else
            close(m_socket);
#endif
            m_socket = INVALID_SOCKET;
        }
    }

    [[nodiscard]] bool IsOpen() const noexcept {
        return m_socket != INVALID_SOCKET;
    }

    // Non-blocking sendto
    int SendTo(const void* data, size_t size, const char* ip, uint16_t port) noexcept {
        if (!IsOpen() || !data || size == 0) return -1;

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
#ifdef _WIN32
        address.sin_addr.s_addr = inet_addr(ip);
#else
        inet_pton(AF_INET, ip, &address.sin_addr);
#endif

        const int bytesSent = sendto(m_socket,
                                     static_cast<const char*>(data),
                                     static_cast<int>(size),
                                     0,
                                     reinterpret_cast<sockaddr*>(&address),
                                     sizeof(address));
        return bytesSent;
    }

    // Non-blocking recvfrom. Returns:
    //  > 0 : bytes received
    //  == 0 : no data available right now (EWOULDBLOCK)
    //  < 0 : socket fatal error
    int RecvFrom(void* buffer, size_t maxBytes, std::string* outIp = nullptr, uint16_t* outPort = nullptr) noexcept {
        if (!IsOpen() || !buffer || maxBytes == 0) return -1;

        sockaddr_in fromAddr{};
#ifdef _WIN32
        int fromLen = sizeof(fromAddr);
#else
        socklen_t fromLen = sizeof(fromAddr);
#endif

        const int bytesRead = recvfrom(m_socket,
                                       static_cast<char*>(buffer),
                                       static_cast<int>(maxBytes),
                                       0,
                                       reinterpret_cast<sockaddr*>(&fromAddr),
                                       &fromLen);

        if (bytesRead > 0) {
            if (outIp) {
                char ipStr[INET_ADDRSTRLEN];
#ifdef _WIN32
                strncpy(ipStr, inet_ntoa(fromAddr.sin_addr), INET_ADDRSTRLEN);
#else
                inet_ntop(AF_INET, &fromAddr.sin_addr, ipStr, sizeof(ipStr));
#endif
                *outIp = ipStr;
            }
            if (outPort) {
                *outPort = ntohs(fromAddr.sin_port);
            }
            return bytesRead;
        }

#ifdef _WIN32
        const int error = WSAGetLastError();
        if (error == WSAEWOULDBLOCK) {
            return 0; // Non-blocking socket has no pending datagrams
        }
#else
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            return 0;
        }
#endif

        return -1; // Socket error
    }

private:
    socket_handle_t m_socket;
};

} // namespace DeltaCompression
