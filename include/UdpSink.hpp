/**
 * @file UdpSink.hpp
 * @brief Streampu Sink module for high-performance UDP transmission.
 */

#ifndef UDP_SINK_HPP
#define UDP_SINK_HPP

#include "UdpSocket.hpp"
#include "UdpPacketizer.hpp"
#include <iostream>

class UdpSink {
private:
    UdpSocket socket_;
    UdpPacketizer packetizer_;
    uint32_t frame_counter_ = 0;

public:
    UdpSink(const std::string& dest_ip, uint16_t dest_port) {
        socket_.set_destination(dest_ip, dest_port);
    }

    /**
     * @brief Sends a full frame to the network.
     * @param data Pointer to the raw data buffer.
     * @param size Size of the data in bytes.
     */
    void send_frame(const void* data, size_t size) {
        // 1. Fragment the data (Zero-Copy)
        // Note: The packetizer prepares struct iovec pointing to 'data'
        size_t packet_count = packetizer_.prepare_frame(data, size, frame_counter_++);

        const auto* packets = packetizer_.get_packets();
        int sockfd = socket_.get_fd();
        const auto* dest = socket_.get_dest_addr();

        // 2. Send Loop
        // Ideally, we would use sendmmsg() here for batching (Linux specific).
        // For compatibility, we use sendmsg() in a loop.
        // Since we tuned SO_SNDBUF, the kernel shouldn't block often.

        struct msghdr msg;
        std::memset(&msg, 0, sizeof(msg));
        msg.msg_name = (void*)dest;
        msg.msg_namelen = sizeof(*dest);

        for (size_t i = 0; i < packet_count; ++i) {
            // Setup the gather vector (Header + Payload)
            // Casting const away is safe here because sendmsg doesn't modify it
            msg.msg_iov = (struct iovec*)packets[i].iov;
            msg.msg_iovlen = 2; // 1 for Header, 1 for Payload

            ssize_t sent = sendmsg(sockfd, &msg, 0);

            if (sent < 0) {
                // Handle EAGAIN (Buffer full) or other errors
                perror("UdpSink: sendmsg failed");
                // In a real-time system, we might choose to drop the packet
                // rather than blocking and creating latency.
            }
        }
    }
};

#endif // UDP_SINK_HPP