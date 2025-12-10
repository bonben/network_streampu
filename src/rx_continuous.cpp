/**
 * @file rx_continuous.cpp
 * @brief Infinite UDP Receiver with Jitter & Throughput Monitoring.
 */

#include <iostream>
#include <vector>
#include <string>
#include <atomic>
#include <thread>
#include <chrono>
#include <cmath>
#include <csignal>
#include <iomanip>
#include <getopt.h>

#include <streampu.hpp>
#include "Source_UDP.hpp"

using namespace spu;
using namespace spu::module;
using namespace spu::runtime;

std::atomic<bool> g_stop_signal(false);
void signal_handler(int) { g_stop_signal = true; }

// Stats
std::atomic<size_t> g_bytes_rcvd(0);
std::atomic<size_t> g_frames_rcvd(0);

// Jitter Calculation Globals
std::atomic<double> g_jitter_ms(0.0);
std::chrono::steady_clock::time_point g_last_arrival;
bool g_first_frame = true;

void monitor_thread() {
    using namespace std::chrono;
    auto last_time = steady_clock::now();
    size_t last_bytes = 0;

    std::cout << std::fixed << std::setprecision(2);

    while (!g_stop_signal) {
        std::this_thread::sleep_for(milliseconds(1000));

        auto now = steady_clock::now();
        double dt = duration_cast<duration<double>>(now - last_time).count();
        size_t current_bytes = g_bytes_rcvd;

        double mbps = (static_cast<double>(current_bytes - last_bytes) * 8.0) / (dt * 1e6);

        std::cout << "\r[RX] Speed: " << std::setw(7) << mbps << " Mbps"
                  << " | Jitter: " << std::setw(5) << g_jitter_ms.load() << " ms"
                  << " | Total: " << g_frames_rcvd << std::flush;

        last_time = now;
        last_bytes = current_bytes;
    }
    std::cout << std::endl;
}

int main(int argc, char** argv) {
    std::signal(SIGINT, signal_handler);

    int port = 9999;
    size_t data_size = 2048;

    int opt;
    while ((opt = getopt(argc, argv, "p:d:h")) != -1) {
        switch (opt) {
            case 'p': port = std::stoi(optarg); break;
            case 'd': data_size = std::stoul(optarg); break;
            case 'h':
                std::cout << "Usage: " << argv[0] << " -p PORT -d SIZE" << std::endl;
                return 0;
        }
    }

    std::cout << "--- Continuous RX Started (Port " << port << ") ---" << std::endl;

    Source_UDP<uint8_t> udp_source(data_size, port);
    Finalizer<uint8_t>  finalizer(data_size);

    finalizer["finalize::in"] = udp_source["generate::out_data"];

    Sequence seq_rx(udp_source("generate"));
    for (auto& mod : seq_rx.get_modules<Module>(false))
        for (auto& tsk : mod->tasks) tsk->set_fast(true);

    std::thread monitor(monitor_thread);

    // Custom execution loop to measure Jitter per frame
    // We cannot use seq_rx.exec() easily if we want to hook into every frame for timing
    // so we use a lambda that runs the sequence once and returns false (keep going).

    seq_rx.exec([&]() {
        // 1. Timestamp arrival
        auto now = std::chrono::steady_clock::now();

        if (!g_first_frame) {
            double delta_us = std::chrono::duration_cast<std::chrono::microseconds>(now - g_last_arrival).count();
            // Simple exponential moving average for Jitter display
            // This represents the "instant" gap between frames.
            // A perfect 60fps stream should show constant ~16.6ms here.
            double delta_ms = delta_us / 1000.0;

            // Optional: Calculate deviation from average if you want variance.
            // For now, displaying Inter-Arrival Time is often called "Jitter" in simple tools.
            g_jitter_ms = (g_jitter_ms * 0.9) + (delta_ms * 0.1);
        } else {
            g_first_frame = false;
        }
        g_last_arrival = now;

        // 2. Update Counters
        g_bytes_rcvd += data_size;
        g_frames_rcvd++;

        return g_stop_signal.load();
    });

    if (monitor.joinable()) monitor.join();
    std::cout << "[RX] Stopped." << std::endl;

    return 0;
}