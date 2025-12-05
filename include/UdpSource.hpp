/**
 * @file UdpSource.hpp
 * @brief Streampu Source module for UDP reception and reassembly.
 */

#ifndef UDP_SOURCE_HPP
#define UDP_SOURCE_HPP

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "UdpSocket.hpp"
#include "UdpReassembler.hpp"
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>

class UdpSource {
private:
    UdpSocket socket_;
    UdpReassembler reassembler_;

    // Threading
    std::thread worker_thread_;
    std::atomic<bool> running_{false};

    // Output Queue (Thread-safe)
    std::queue<std::vector<uint8_t>> completed_frames_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;

    // Temporary receive buffer (stack allocated or reusable heap buffer)
    // Size = Header + Payload + padding safety
    static const size_t RX_BUFFER_SIZE = sizeof(StreampuHeader) + STREAMPU_MAX_PAYLOAD + 64;

public:
    UdpSource(uint16_t listen_port) {
        socket_.bind_port(listen_port);
        // Set timeout to 100ms to allow checking 'running_' flag periodically
        socket_.set_recv_timeout(100);
    }

    ~UdpSource() {
        stop();
    }

    void start() {
        if (running_) return;
        running_ = true;
        worker_thread_ = std::thread(&UdpSource::receive_loop, this);
    }

    void stop() {
        if (!running_) return;
        running_ = false;
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }

    /**
    * @brief Retrieve the next available frame with a timeout.
    * @param timeout_ms Max wait time in milliseconds.
    * If < 0, waits indefinitely (Blocking).
    * If 0, non-blocking check.
    * @return A vector containing the frame data, or empty vector if timeout/stopped.
    */
    std::vector<uint8_t> pop_frame(int timeout_ms = -1) {
        std::unique_lock<std::mutex> lock(queue_mutex_);

        // Predicate: Data available OR Source stopped
        auto ready_pred = [this] { return !completed_frames_.empty() || !running_; };

        bool data_available = false;

        if (timeout_ms < 0) {
            // Infinite wait
            queue_cv_.wait(lock, ready_pred);
            data_available = !completed_frames_.empty();
        } else {
            // Wait with timeout
            data_available = queue_cv_.wait_for(
                lock,
                std::chrono::milliseconds(timeout_ms),
                ready_pred
            );
            // Note: wait_for returns false if timeout occurred AND predicate is false
            // But we must double check if data is really there (race condition safety)
            if (data_available) {
                data_available = !completed_frames_.empty();
            }
        }

        if (data_available) {
            std::vector<uint8_t> frame = std::move(completed_frames_.front());
            completed_frames_.pop();
            return frame;
        }

        // Return empty vector on timeout or stop
        return {};
    }

private:
void receive_loop() {
        // BATCH CONFIGURATION
        // Reading 64 packets per syscall is a sweet spot for latency/throughput
        const int BATCH_SIZE = 64;

        // Structures required by recvmmsg
        struct mmsghdr msgs[BATCH_SIZE];
        struct iovec iovecs[BATCH_SIZE];

        // One giant buffer to hold 64 packets contiguously (Cache friendly)
        std::vector<uint8_t> rx_buffer_pool(BATCH_SIZE * RX_BUFFER_SIZE);

        // Prepare the structures once (Zero-Copy setup)
        for (int i = 0; i < BATCH_SIZE; ++i) {
            std::memset(&iovecs[i], 0, sizeof(struct iovec));
            std::memset(&msgs[i], 0, sizeof(struct mmsghdr));

            // Point each slot to a slice of our giant buffer
            iovecs[i].iov_base = &rx_buffer_pool[i * RX_BUFFER_SIZE];
            iovecs[i].iov_len = RX_BUFFER_SIZE;

            msgs[i].msg_hdr.msg_iov = &iovecs[i];
            msgs[i].msg_hdr.msg_iovlen = 1;
        }

        int fd = socket_.get_fd();

        // Timeout configuration for recvmmsg (1 second)
        struct timespec timeout;
        timeout.tv_sec = 1;
        timeout.tv_nsec = 0;

        while (running_) {
            // THE MAGIC CALL
            // Asks Linux: "Fill as many of these 64 slots as you can, right now."
            // It returns immediately if data is there, or waits up to timeout.
            int retval = recvmmsg(fd, msgs, BATCH_SIZE, 0, &timeout);

            if (retval < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
                perror("UdpSource: recvmmsg failed");
                break;
            }
            if (retval == 0) continue; // Timeout

            // 'retval' is the number of packets actually received (e.g., 42)
            for (int i = 0; i < retval; ++i) {
                // Get the actual length of packet i
                size_t len = msgs[i].msg_len;

                // Sanity check
                if (len < sizeof(StreampuHeader)) continue;

                // Locate the data in our pool
                uint8_t* pkt_data = &rx_buffer_pool[i * RX_BUFFER_SIZE];
                const StreampuHeader* header = reinterpret_cast<const StreampuHeader*>(pkt_data);
                const uint8_t* payload = pkt_data + sizeof(StreampuHeader);

                // Process
                auto result = reassembler_.add_fragment(*header, payload, len - sizeof(StreampuHeader));

                if (result.complete) {
                    {
                        std::lock_guard<std::mutex> lock(queue_mutex_);
                        completed_frames_.push(std::move(result.data));
                    }
                    queue_cv_.notify_one();
                }

                // Reset header length for next run (Linux might modify it)
                msgs[i].msg_len = 0;
            }
        }
    }
};

#endif // UDP_SOURCE_HPP