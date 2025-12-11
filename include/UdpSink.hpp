/**
 * @file UdpSink.hpp
 * @brief Streampu Sink module for high-performance UDP transmission.
 * * OPTIMIZATION: Uses sendmmsg (Linux) for batch transmission.
 */

#ifndef UDP_SINK_HPP
#define UDP_SINK_HPP

#ifndef _GNU_SOURCE
#define _GNU_SOURCE // Required for sendmmsg/mmsghdr
#endif

#include "UdpSocket.hpp"
#include "UdpPacketizer.hpp"
#include <iostream>
#include <vector>

class UdpSink {
private:
    UdpSocket socket_;
    UdpPacketizer packetizer_;
    uint32_t frame_counter_ = 0;

    // Buffer for batch sending
    // We reuse this vector to avoid reallocating mmsghdr structs every frame
    std::vector<struct mmsghdr> msg_vec_;

public:
    UdpSink(const std::string& dest_ip, uint16_t dest_port) {
        socket_.set_destination(dest_ip, dest_port);
        // Pre-allocate enough headers for a large frame (e.g. 8000 packets)
        msg_vec_.reserve(8000);
    }

    /**
     * @brief Sends a full frame to the network using batching.
     * @param data Pointer to the raw data buffer.
     * @param size Size of the data in bytes.
     */
    void send_frame(const void* data, size_t size) {
        // 1. Fragment the data (Zero-Copy)
        size_t packet_count = packetizer_.prepare_frame(data, size, frame_counter_++);

        const auto* packets = packetizer_.get_packets();
        int sockfd = socket_.get_fd();
        const auto* dest = socket_.get_dest_addr();

        // 2. Prepare Batch Structures (Zero-Copy)
        // We only need to resize the vector of headers, not reallocate the payloads
        if (msg_vec_.size() < packet_count) {
            msg_vec_.resize(packet_count);
        }

        // We fill the mmsghdr structures pointing to the packetizer's iovecs
        for (size_t i = 0; i < packet_count; ++i) {
            auto& msg_hdr = msg_vec_[i].msg_hdr;

            // Point to the packetizer's scatter/gather array
            // Casting const away is necessary for the API, but kernel reads only.
            msg_hdr.msg_iov = (struct iovec*)packets[i].iov;
            msg_hdr.msg_iovlen = 2; // Header + Payload

            // Destination address (Same for all packets)
            msg_hdr.msg_name = (void*)dest;
            msg_hdr.msg_namelen = sizeof(*dest);

            // Reset control fields
            msg_hdr.msg_control = nullptr;
            msg_hdr.msg_controllen = 0;
            msg_hdr.msg_flags = 0;
        }

        // 3. Batch Send Loop
        // sendmmsg can handle the whole batch, but sometimes returns partials.
        size_t sent_packets = 0;
        while (sent_packets < packet_count) {
            // Send remaining packets in one syscall
            int retval = sendmmsg(sockfd, &msg_vec_[sent_packets], packet_count - sent_packets, 0);

            if (retval < 0) {
                if (errno == EINTR) continue;
                // If buffer is full (EAGAIN), we might want to yield or retry
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // Simple busy-wait/yield strategy for low latency
                    // std::this_thread::yield();
                    continue;
                }
                perror("UdpSink: sendmmsg failed");
                break; // Fatal error
            }

            sent_packets += retval;
        }
    }
};

#endif // UDP_SINK_HPP