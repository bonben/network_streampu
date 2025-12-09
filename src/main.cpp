#include <iostream>
#include <vector>
#include <numeric>
#include <thread>
#include <chrono>
#include <atomic>
#include <iomanip>
#include <cstring>

#include "UdpSink.hpp"
#include "UdpSource.hpp"

// Default Configuration
const uint16_t PORT = 9999;
const std::string IP = "127.0.0.1";

// Atomic counters for monitoring
std::atomic<size_t> g_bytes_sent{0};
std::atomic<size_t> g_bytes_received{0};
std::atomic<size_t> g_frames_sent{0};
std::atomic<size_t> g_frames_received{0};

// Helper to calculate throughput
double calculate_mbps(size_t bytes, double seconds) {
    if (seconds <= 0) return 0.0;
    return (static_cast<double>(bytes) * 8.0) / (1000.0 * 1000.0 * seconds);
}

void rx_thread_func(int expected_frames, size_t expected_size) {
    std::cout << "[RX-Thread] Initializing Source on Port " << PORT << "..." << std::endl;

    // Initialize Source
    UdpSource source(PORT);
    source.start();

    std::cout << "[RX-Thread] Ready and listening." << std::endl;

    // Timeout loop counters
    int timeout_counter = 0;
    const int MAX_TIMEOUTS = 5; // Exit if no data for 5 seconds after start

    while (g_frames_received < expected_frames) {
        // Wait up to 1000ms for a frame
        std::vector<uint8_t> frame = source.pop_frame(1000);

        if (!frame.empty()) {
            // Verify size
            if (frame.size() != expected_size) {
                std::cerr << "[RX-Thread] Error: Frame size mismatch! Expected "
                          << expected_size << ", got " << frame.size() << std::endl;
            }

            // Update stats
            g_bytes_received += frame.size();
            g_frames_received++;
            timeout_counter = 0; // Reset timeout
        } else {
            // Check if we should abort (only if we have already started receiving or TX is presumably done)
            if (timeout_counter++ >= MAX_TIMEOUTS) {
                std::cout << "[RX-Thread] Timed out waiting for data." << std::endl;
                break;
            }
        }
    }

    source.stop();
    std::cout << "[RX-Thread] Finished." << std::endl;
}

void tx_thread_func(int num_frames, size_t frame_size) {
    std::cout << "[TX-Thread] Initializing Sink targeting " << IP << ":" << PORT << "..." << std::endl;

    // Initialize Sink
    UdpSink sink(IP, PORT);

    // Prepare dummy data (allocate once to avoid measuring allocation time)
    std::vector<uint8_t> tx_data(frame_size);
    // Fill with pattern
    std::iota(tx_data.begin(), tx_data.end(), 0);

    // Give RX thread a moment to bind the socket
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::cout << "[TX-Thread] Sending " << num_frames << " frames of " << frame_size << " bytes..." << std::endl;

    for (int i = 0; i < num_frames; ++i) {
        sink.send_frame(tx_data.data(), tx_data.size());

        g_bytes_sent += tx_data.size();
        g_frames_sent++;

        // Optional: Small yield to prevent totally overwhelming loopback buffers on very small frames
        // std::this_thread::yield();
    }

    std::cout << "[TX-Thread] Finished sending." << std::endl;
}

int main(int argc, char* argv[]) {
    // 1. Parse Arguments
    int num_frames = 100;           // Default: 100 frames
    size_t frame_size = 1024 * 1024; // Default: 1 MB

    if (argc >= 2) {
        num_frames = std::stoi(argv[1]);
    }
    if (argc >= 3) {
        frame_size = std::stoul(argv[2]);
    }

    std::cout << "=========================================" << std::endl;
    std::cout << " Streampu UDP Throughput Test " << std::endl;
    std::cout << " Frames: " << num_frames << std::endl;
    std::cout << " Size:   " << frame_size << " bytes" << std::endl;
    std::cout << "=========================================" << std::endl;

    // 2. Start Threads
    auto start_time = std::chrono::high_resolution_clock::now();

    // Start RX first to ensure socket is bound
    std::thread rx_thread(rx_thread_func, num_frames, frame_size);
    std::thread tx_thread(tx_thread_func, num_frames, frame_size);

    // 3. Wait for completion
    tx_thread.join();
    rx_thread.join();

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = end_time - start_time;

    // 4. Report Results
    std::cout << "\n=========================================" << std::endl;
    std::cout << " Test Results " << std::endl;
    std::cout << "=========================================" << std::endl;
    std::cout << "Duration:       " << duration.count() << " seconds" << std::endl;

    std::cout << "\n[TX Stats]" << std::endl;
    std::cout << "Frames Sent:    " << g_frames_sent << std::endl;
    std::cout << "Bytes Sent:     " << g_bytes_sent / (1024.0 * 1024.0) << " MB" << std::endl;
    std::cout << "Throughput:     " << calculate_mbps(g_bytes_sent, duration.count()) << " Mbps" << std::endl;

    std::cout << "\n[RX Stats]" << std::endl;
    std::cout << "Frames Rcvd:    " << g_frames_received << std::endl;
    std::cout << "Bytes Rcvd:     " << g_bytes_received / (1024.0 * 1024.0) << " MB" << std::endl;
    std::cout << "Throughput:     " << calculate_mbps(g_bytes_received, duration.count()) << " Mbps" << std::endl;

    // Loss Calculation
    size_t frames_lost = g_frames_sent - g_frames_received;
    double loss_percent = (g_frames_sent > 0) ? (static_cast<double>(frames_lost) / g_frames_sent) * 100.0 : 0.0;

    std::cout << "\n[Quality]" << std::endl;
    std::cout << "Frame Loss:     " << frames_lost << " (" << std::fixed << std::setprecision(2) << loss_percent << "%)" << std::endl;

    return (frames_lost == 0) ? 0 : 1;
}