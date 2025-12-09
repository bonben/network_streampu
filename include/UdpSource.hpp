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
#include "UdpReassembler.hpp" // Implicitly pulls in spu_udp_protocol.h
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
    // UPDATED: Using SpuUdpHeader and SPU_UDP_MAX_PAYLOAD
    static const size_t RX_BUFFER_SIZE = sizeof(SpuUdpHeader) + SPU_UDP_MAX_PAYLOAD + 64;

public:
    UdpSource(uint16_t listen_port) {
        socket_.bind_port(listen_port);
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

    std::vector<uint8_t> pop_frame(int timeout_ms = -1) {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        auto ready_pred = [this] { return !completed_frames_.empty() || !running_; };
        bool data_available = false;

        if (timeout_ms < 0) {
            queue_cv_.wait(lock, ready_pred);
            data_available = !completed_frames_.empty();
        } else {
            data_available = queue_cv_.wait_for(
                lock,
                std::chrono::milliseconds(timeout_ms),
                ready_pred
            );
            if (data_available) {
                data_available = !completed_frames_.empty();
            }
        }

        if (data_available) {
            std::vector<uint8_t> frame = std::move(completed_frames_.front());
            completed_frames_.pop();
            return frame;
        }
        return {};
    }

private:
void receive_loop() {
        const int BATCH_SIZE = 64;

        struct mmsghdr msgs[BATCH_SIZE];
        struct iovec iovecs[BATCH_SIZE];
        std::vector<uint8_t> rx_buffer_pool(BATCH_SIZE * RX_BUFFER_SIZE);

        for (int i = 0; i < BATCH_SIZE; ++i) {
            std::memset(&iovecs[i], 0, sizeof(struct iovec));
            std::memset(&msgs[i], 0, sizeof(struct mmsghdr));
            iovecs[i].iov_base = &rx_buffer_pool[i * RX_BUFFER_SIZE];
            iovecs[i].iov_len = RX_BUFFER_SIZE;
            msgs[i].msg_hdr.msg_iov = &iovecs[i];
            msgs[i].msg_hdr.msg_iovlen = 1;
        }

        int fd = socket_.get_fd();

        while (running_) {
            struct timespec timeout;
            timeout.tv_sec = 1;
            timeout.tv_nsec = 0;

            int retval = recvmmsg(fd, msgs, BATCH_SIZE, 0, &timeout);

            if (retval < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
                perror("UdpSource: recvmmsg failed");
                break;
            }
            if (retval == 0) continue;

            for (int i = 0; i < retval; ++i) {
                size_t len = msgs[i].msg_len;

                // Sanity check with updated header size
                if (len < sizeof(SpuUdpHeader)) continue;

                uint8_t* pkt_data = &rx_buffer_pool[i * RX_BUFFER_SIZE];

                // Cast to new Header type
                const SpuUdpHeader* header = reinterpret_cast<const SpuUdpHeader*>(pkt_data);
                const uint8_t* payload = pkt_data + sizeof(SpuUdpHeader);

                auto result = reassembler_.add_fragment(*header, payload, len - sizeof(SpuUdpHeader));

                if (result.complete) {
                    {
                        std::lock_guard<std::mutex> lock(queue_mutex_);
                        completed_frames_.push(std::move(result.data));
                    }
                    queue_cv_.notify_one();
                }
                msgs[i].msg_len = 0;
            }
        }
    }
};

#endif // UDP_SOURCE_HPP