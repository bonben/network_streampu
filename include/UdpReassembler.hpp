/**
 * @file UdpReassembler.hpp
 * @brief Logic for reassembling fragmented UDP frames for Streampu.
 * * FIX APPLIED: Correctly handles exact frame size by detecting the length of the last fragment.
 */

#ifndef UDP_REASSEMBLER_HPP
#define UDP_REASSEMBLER_HPP

#include "streampu_protocol.h"
#include <vector>
#include <map>
#include <cstring>
#include <iostream>
#include <chrono>

class UdpReassembler {
public:
    struct Result {
        bool complete;
        std::vector<uint8_t> data;
        uint32_t frame_id;
    };

private:
    struct IncompleteFrame {
        std::vector<uint8_t> buffer;
        std::vector<bool> received_mask;
        size_t received_count;
        uint16_t total_frags;
        size_t final_data_size; // <--- NEW: Stores the detected exact size
        std::chrono::steady_clock::time_point last_update;
    };

    std::map<uint32_t, IncompleteFrame> pending_frames_;

    const size_t MAX_PENDING_FRAMES = 10;
    const int FRAME_TIMEOUT_MS = 1000;

public:
    UdpReassembler() = default;

    Result add_fragment(const StreampuHeader& header, const void* payload, size_t payload_len) {
        Result res = {false, {}, header.frame_id};

        if (payload_len > STREAMPU_MAX_PAYLOAD) return res;

        // Cleanup logic...
        if (pending_frames_.size() >= MAX_PENDING_FRAMES) {
             cleanup_old_frames();
             if (pending_frames_.find(header.frame_id) == pending_frames_.end()) return res;
        }

        auto it = pending_frames_.find(header.frame_id);

        if (it == pending_frames_.end()) {
            IncompleteFrame new_frame;

            // Allocate max theoretical size initially
            size_t total_max_size = static_cast<size_t>(header.total_frags) * STREAMPU_MAX_PAYLOAD;

            if (total_max_size > STREAMPU_MAX_FRAME_SIZE) return res;

            try {
                new_frame.buffer.resize(total_max_size);
                new_frame.received_mask.resize(header.total_frags, false);
            } catch (const std::bad_alloc&) { return res; }

            new_frame.total_frags = header.total_frags;
            new_frame.received_count = 0;
            new_frame.final_data_size = total_max_size; // Default to max
            new_frame.last_update = std::chrono::steady_clock::now();

            auto insert_res = pending_frames_.insert({header.frame_id, std::move(new_frame)});
            it = insert_res.first;
        }

        IncompleteFrame& frame = it->second;
        frame.last_update = std::chrono::steady_clock::now();

        if (header.frag_index >= frame.total_frags) return res;
        if (frame.received_mask[header.frag_index]) return res;

        // --- NEW LOGIC START ---

        // 1. Copy Data
        size_t offset = static_cast<size_t>(header.frag_index) * STREAMPU_MAX_PAYLOAD;
        if (offset + payload_len <= frame.buffer.size()) {
            std::memcpy(frame.buffer.data() + offset, payload, payload_len);
        }

        // 2. If this is the LAST fragment, we found the real end of the frame!
        if (header.frag_index == header.total_frags - 1) {
            frame.final_data_size = offset + payload_len;
        }

        // --- NEW LOGIC END ---

        frame.received_mask[header.frag_index] = true;
        frame.received_count++;

        if (frame.received_count == frame.total_frags) {
            // Trim the buffer to the exact size detected from the last fragment
            if (frame.final_data_size < frame.buffer.size()) {
                frame.buffer.resize(frame.final_data_size);
            }

            res.complete = true;
            res.data = std::move(frame.buffer);
            pending_frames_.erase(it);
        }

        return res;
    }

private:
    void cleanup_old_frames() {
        auto now = std::chrono::steady_clock::now();
        for (auto it = pending_frames_.begin(); it != pending_frames_.end(); ) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second.last_update).count();
            if (elapsed > FRAME_TIMEOUT_MS) it = pending_frames_.erase(it);
            else ++it;
        }
        if (pending_frames_.size() >= MAX_PENDING_FRAMES) pending_frames_.erase(pending_frames_.begin());
    }
};

#endif // UDP_REASSEMBLER_HPP