/**
 * @file tx_continuous.cpp
 * @brief Infinite UDP Transmitter with Throughput Monitoring.
 */

#include <iostream>
#include <vector>
#include <string>
#include <atomic>
#include <thread>
#include <chrono>
#include <numeric>
#include <csignal>
#include <iomanip>
#include <getopt.h>

#include <streampu.hpp>
#include "Sink_UDP.hpp"

using namespace spu;
using namespace spu::module;
using namespace spu::runtime;

// Global stop signal for Ctrl+C
std::atomic<bool> g_stop_signal(false);
void signal_handler(int) { g_stop_signal = true; }

// Monitoring variables
std::atomic<size_t> g_bytes_sent(0);
std::atomic<size_t> g_frames_sent(0);

void monitor_thread() {
    using namespace std::chrono;
    auto last_time = steady_clock::now();
    size_t last_bytes = 0;

    std::cout << std::fixed << std::setprecision(2);

    while (!g_stop_signal) {
        std::this_thread::sleep_for(milliseconds(1000));

        auto now = steady_clock::now();
        double dt = duration_cast<duration<double>>(now - last_time).count();
        size_t current_bytes = g_bytes_sent;

        double mbps = (static_cast<double>(current_bytes - last_bytes) * 8.0) / (dt * 1e6);

        std::cout << "\r[TX] Speed: " << mbps << " Mbps | Frames: " << g_frames_sent << std::flush;

        last_time = now;
        last_bytes = current_bytes;
    }
    std::cout << std::endl;
}

int main(int argc, char** argv) {
    std::signal(SIGINT, signal_handler);

    std::string ip = "127.0.0.1";
    int port = 9999;
    size_t data_size = 2048;

    // --- Simple Arg Parsing ---
    int opt;
    while ((opt = getopt(argc, argv, "i:p:d:h")) != -1) {
        switch (opt) {
            case 'i': ip = optarg; break;
            case 'p': port = std::stoi(optarg); break;
            case 'd': data_size = std::stoul(optarg); break;
            case 'h':
                std::cout << "Usage: " << argv[0] << " -i IP -p PORT -d SIZE" << std::endl;
                return 0;
        }
    }

    std::cout << "--- Continuous TX Started (" << ip << ":" << port << ") ---" << std::endl;
    std::cout << "Press Ctrl+C to stop." << std::endl;

    // Modules
    Initializer<uint8_t> initializer(data_size);
    Sink_UDP<uint8_t>    udp_sink(data_size, ip, port);

    // Data Init
    std::vector<std::vector<uint8_t>> init_data(1, std::vector<uint8_t>(data_size));
    std::iota(init_data[0].begin(), init_data[0].end(), 0);
    initializer.set_init_data(init_data);

    // Binding
    udp_sink["send::in_data"] = initializer["initialize::out"];

    // Sequence
    Sequence seq_tx(initializer("initialize"));
    for (auto& mod : seq_tx.get_modules<Module>(false))
        for (auto& tsk : mod->tasks) tsk->set_fast(true);

    // Start Monitor
    std::thread monitor(monitor_thread);

    // Execution Loop
    seq_tx.exec([&]() {
        g_bytes_sent += data_size;
        g_frames_sent++;
        return g_stop_signal.load(); // Stop when true
    });

    if (monitor.joinable()) monitor.join();
    std::cout << "[TX] Stopped." << std::endl;

    return 0;
}