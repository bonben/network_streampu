/**
 * @file rx_benchmark.cpp
 * @brief Pure Performance Receiver using recvmmsg (Linux).
 * Discards data immediately to measure raw kernel/network speed.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <iostream>
#include <vector>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

// Configuration
const int PORT = 9999;
const int BATCH_SIZE = 1024; // Huge batch for max throughput
const size_t PKT_SIZE = 2048; // Max buffer per packet

std::atomic<size_t> g_bytes(0);
std::atomic<size_t> g_packets(0);

void monitor() {
    auto last_t = std::chrono::steady_clock::now();
    size_t last_b = 0;

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration_cast<std::chrono::duration<double>>(now - last_t).count();

        size_t cur_b = g_bytes.load();
        size_t cur_p = g_packets.load();

        double gbps = ((cur_b - last_b) * 8.0) / (dt * 1e9);
        double mpps = (cur_p / 1e6) / dt; // Million Packets Per Second (accumulated count logic is simplified here)

        // Reset packet counter for PPS (approx) or keep cumulative?
        // Let's use simple diff logic
        static size_t last_p_count = 0;
        double pps = (double)(cur_p - last_p_count) / dt;
        last_p_count = cur_p;

        std::cout << "RX Speed: " << std::fixed << std::setprecision(2)
                  << gbps << " Gbps | "
                  << std::setprecision(3) << (pps / 1e6) << " Mpps" << std::endl;

        last_b = cur_b;
        last_t = now;
    }
}

int main() {
    // 1. Setup Socket
    int fd = socket(AF_INET, SOCK_DGRAM, 0);

    // Huge Kernel Buffer
    int buf_size = 33554432; // 32MB
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buf_size, sizeof(buf_size));

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Bind failed");
        return 1;
    }

    // 2. Prepare recvmmsg structures
    struct mmsghdr msgs[BATCH_SIZE];
    struct iovec iovecs[BATCH_SIZE];
    std::vector<uint8_t> buffer_pool(BATCH_SIZE * PKT_SIZE);

    for (int i = 0; i < BATCH_SIZE; ++i) {
        std::memset(&iovecs[i], 0, sizeof(iovecs[i]));
        std::memset(&msgs[i], 0, sizeof(msgs[i]));
        iovecs[i].iov_base = &buffer_pool[i * PKT_SIZE];
        iovecs[i].iov_len = PKT_SIZE;
        msgs[i].msg_hdr.msg_iov = &iovecs[i];
        msgs[i].msg_hdr.msg_iovlen = 1;
    }

    std::thread stat_thread(monitor);
    std::cout << "Benchmarks RX running on port " << PORT << "..." << std::endl;

    // 3. Hot Loop
    while (true) {
        // Block until at least 1 packet arrives, then grab up to 1024
        int retval = recvmmsg(fd, msgs, BATCH_SIZE, 0, nullptr);

        if (retval > 0) {
            size_t batch_bytes = 0;
            for (int i = 0; i < retval; ++i) {
                batch_bytes += msgs[i].msg_len;
                // Reset for next call? Not strictly needed for len/flags usually but good practice
                msgs[i].msg_len = 0;
            }
            g_bytes += batch_bytes;
            g_packets += retval;
        }
    }

    return 0;
}