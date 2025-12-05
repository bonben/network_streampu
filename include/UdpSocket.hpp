/**
 * @file UdpSocket.hpp
 * @brief RAII wrapper for POSIX UDP sockets with high-performance tuning.
 */

#ifndef UDP_SOCKET_HPP
#define UDP_SOCKET_HPP

#include <string>
#include <stdexcept>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

class UdpSocket {
private:
    int sockfd_ = -1;
    struct sockaddr_in dest_addr_;
    bool is_bound_ = false;

public:
    UdpSocket() {
        sockfd_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (sockfd_ < 0) {
            throw std::runtime_error("UdpSocket: Failed to create socket");
        }

        // 1. Optimize Kernel Buffers (Critical for high throughput)
        // Set to 32 MB. If the kernel limit is lower, it will be capped silently.
        // Note: You might need to run `sysctl -w net.core.rmem_max=33554432` on Linux.
        int buf_size = 32 * 1024 * 1024;
        setsockopt(sockfd_, SOL_SOCKET, SO_RCVBUF, &buf_size, sizeof(buf_size));
        setsockopt(sockfd_, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size));

        // DEBUG: Check actual buffer size
        int actual_rcv_buf = 0;
        socklen_t optlen = sizeof(actual_rcv_buf);
        if (getsockopt(sockfd_, SOL_SOCKET, SO_RCVBUF, &actual_rcv_buf, &optlen) == 0) {
            std::cout << "[DEBUG] Kernel RCVBUF: " << actual_rcv_buf / 1024 << " KB" << std::endl;
        }
    }

    ~UdpSocket() {
        if (sockfd_ >= 0) {
            close(sockfd_);
        }
    }

    // Disable copy to avoid double-close
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    /**
     * @brief Server Mode: Bind to a specific local port.
     */
    void bind_port(uint16_t port) {
        struct sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY); // Listen on all interfaces
        addr.sin_port = htons(port);

        // Allow restarting the server immediately after a crash
        int opt = 1;
        setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        if (bind(sockfd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            throw std::runtime_error("UdpSocket: Failed to bind port " + std::to_string(port));
        }
        is_bound_ = true;
    }

    /**
     * @brief Client Mode: Set the default destination.
     */
    void set_destination(const std::string& ip, uint16_t port) {
        std::memset(&dest_addr_, 0, sizeof(dest_addr_));
        dest_addr_.sin_family = AF_INET;
        dest_addr_.sin_port = htons(port);
        if (inet_pton(AF_INET, ip.c_str(), &dest_addr_.sin_addr) <= 0) {
            throw std::runtime_error("UdpSocket: Invalid IP address " + ip);
        }
    }

    /**
     * @brief Set a reception timeout (to unblock recv loop cleanly).
     */
    void set_recv_timeout(int timeout_ms) {
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        setsockopt(sockfd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    int get_fd() const { return sockfd_; }
    const struct sockaddr_in* get_dest_addr() const { return &dest_addr_; }
};

#endif // UDP_SOCKET_HPP