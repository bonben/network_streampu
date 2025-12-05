/**
 * @file UdpPacketizer.hpp
 * @brief Zero-Copy fragmentation engine for Streampu UDP protocol.
 * * This class transforms a large data buffer into a sequence of small UDP packets
 * without copying the payload data (Zero-Copy using scatter/gather I/O).
 */

#ifndef UDP_PACKETIZER_HPP
#define UDP_PACKETIZER_HPP

#include "streampu_protocol.h"
#include <vector>
#include <sys/uio.h> // For struct iovec
#include <stdexcept>
#include <cmath>
#include <algorithm> // For std::min

class UdpPacketizer {
public:
    // Represents a ready-to-send packet
    struct Packet {
        StreampuHeader header; // Local storage for the header
        struct iovec iov[2];   // Vector for sendmsg (0: Header, 1: Payload)
    };

private:
    // Pool of pre-allocated packets to avoid dynamic allocation in the hot loop
    std::vector<Packet> packet_pool_;

    // Number of valid packets for the current frame
    size_t current_count_ = 0;

public:
    UdpPacketizer() {
        // Pre-allocate for ~10 MB frame size (approx 7500 packets)
        // This prevents vector resizing during runtime.
        packet_pool_.reserve(8000);
    }

    /**
     * @brief Prepares a frame for transmission by fragmenting it.
     * * @param data Pointer to the raw user data buffer.
     * @param size Total size of the data in bytes.
     * @param frame_id The unique ID for this frame.
     * @return The number of fragments generated.
     */
    size_t prepare_frame(const void* data, size_t size, uint32_t frame_id) {
        if (size > STREAMPU_MAX_FRAME_SIZE) {
            throw std::runtime_error("Streampu: Frame too large for protocol limits");
        }

        // 1. Calculate required fragments
        // Integer division rounded up: ceil(size / payload_max)
        size_t total_frags = (size + STREAMPU_MAX_PAYLOAD - 1) / STREAMPU_MAX_PAYLOAD;

        // Edge case: If size is 0 (empty frame), we still send 1 packet with 0 payload
        if (total_frags == 0) total_frags = 1;

        // 2. Expand pool if necessary (should happen rarely after warmup)
        if (total_frags > packet_pool_.size()) {
            packet_pool_.resize(total_frags);
        }

        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        size_t remaining = size;
        size_t offset = 0;

        // 3. Fragmentation Loop
        // We iterate through the pool and configure pointers. No data copy happens here.
        for (size_t i = 0; i < total_frags; ++i) {
            Packet& p = packet_pool_[i];

            // A. Fill the Header
            p.header.frame_id = frame_id;
            p.header.frag_index = static_cast<uint16_t>(i);
            p.header.total_frags = static_cast<uint16_t>(total_frags);

            // B. Calculate Payload Chunk Size
            size_t chunk_size = std::min(remaining, static_cast<size_t>(STREAMPU_MAX_PAYLOAD));

            // C. Configure I/O Vector (Scatter/Gather)
            // Element 0: Points to our local header
            p.iov[0].iov_base = &p.header;
            p.iov[0].iov_len = sizeof(StreampuHeader);

            // Element 1: Points to the user's buffer slice
            // const_cast is required by POSIX api, but sendmsg won't modify it.
            p.iov[1].iov_base = const_cast<void*>(static_cast<const void*>(bytes + offset));
            p.iov[1].iov_len = chunk_size;

            // Advance cursors
            offset += chunk_size;
            remaining -= chunk_size;
        }

        current_count_ = total_frags;
        return current_count_;
    }

    /**
     * @brief Access the prepared packets.
     * @return Pointer to the array of Packet structures.
     */
    const Packet* get_packets() const {
        return packet_pool_.data();
    }

    /**
     * @brief Get the number of packets ready to send.
     */
    size_t get_count() const {
        return current_count_;
    }
};

#endif // UDP_PACKETIZER_HPP