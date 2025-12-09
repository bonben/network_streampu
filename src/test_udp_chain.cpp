/**
 * @file test_udp_chain.cpp
 * @brief Integration test for StreamPU with UDP Source/Sink modules.
 */

#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <algorithm>
#include <thread>
#include <atomic>
#include <numeric>
#include <getopt.h>

#include <streampu.hpp>

#include "Source_UDP.hpp"
#include "Sink_UDP.hpp"

using namespace spu;
using namespace spu::module;
using namespace spu::runtime;

// Default Configuration
const uint16_t PORT_DEFAULT = 9999;
const std::string IP_DEFAULT = "127.0.0.1";

void print_help(char** argv) {
    std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
    std::cout << "  -n, --n-frames        Number of frames to process [100]" << std::endl;
    std::cout << "  -d, --data-size       Size of data in bytes [2048]" << std::endl;
    std::cout << "  -p, --print-stats     Enable per-task statistics [false]" << std::endl;
    std::cout << "  -g, --debug           Enable task debug mode (print socket data) [false]" << std::endl;
    std::cout << "  -h, --help            Show this help message" << std::endl;
}

int main(int argc, char** argv)
{
    // -------------------------------------------------------------------------
    // 0. Argument Parsing
    // -------------------------------------------------------------------------
    size_t n_frames = 100;
    size_t data_size = 2048;
    bool print_stats = false;
    bool debug = false;

    struct option longopts[] = {
        { "n-frames",    required_argument, NULL, 'n' },
        { "data-size",   required_argument, NULL, 'd' },
        { "print-stats", no_argument,       NULL, 'p' },
        { "debug",       no_argument,       NULL, 'g' },
        { "help",        no_argument,       NULL, 'h' },
        { NULL,          0,                 NULL, 0   }
    };

    while (true) {
        const int opt = getopt_long(argc, argv, "n:d:pgh", longopts, 0);
        if (opt == -1) break;
        switch (opt) {
            case 'n': n_frames = std::stoi(optarg); break;
            case 'd': data_size = std::stoi(optarg); break;
            case 'p': print_stats = true; break;
            case 'g': debug = true; break;
            case 'h': print_help(argv); return 0;
            default: break;
        }
    }

    std::cout << "#########################################" << std::endl;
    std::cout << "# StreamPU UDP Integration Test         #" << std::endl;
    std::cout << "#########################################" << std::endl;
    std::cout << "Frames:    " << n_frames << std::endl;
    std::cout << "Data Size: " << data_size << std::endl;
    std::cout << "Stats:     " << (print_stats ? "ON" : "OFF") << std::endl;
    std::cout << "Debug:     " << (debug ? "ON" : "OFF") << std::endl;

    // -------------------------------------------------------------------------
    // 1. Modules Creation
    // -------------------------------------------------------------------------

    Initializer<uint8_t> initializer(data_size);
    Sink_UDP<uint8_t>    udp_sink(data_size, IP_DEFAULT, PORT_DEFAULT);
    Source_UDP<uint8_t>  udp_source(data_size, PORT_DEFAULT);
    Finalizer<uint8_t>   finalizer(data_size);

    // -------------------------------------------------------------------------
    // 2. Data Initialization
    // -------------------------------------------------------------------------

    std::vector<std::vector<uint8_t>> init_data(1, std::vector<uint8_t>(data_size));
    std::iota(init_data[0].begin(), init_data[0].end(), 0);
    initializer.set_init_data(init_data);

    // -------------------------------------------------------------------------
    // 3. Chain Binding
    // -------------------------------------------------------------------------

    // TX Chain
    udp_sink["send::in_data"] = initializer["initialize::out"];

    // RX Chain
    finalizer["finalize::in"] = udp_source["generate::out_data"];

    // -------------------------------------------------------------------------
    // 4. Sequence Creation & Configuration
    // -------------------------------------------------------------------------

    Sequence seq_tx(initializer("initialize"));
    Sequence seq_rx(udp_source("generate"));

    // Helper to configure tasks for stats/debug
    auto configure_tasks = [&](Sequence& seq) {
        for (auto& mod : seq.get_modules<Module>(false)) {
            for (auto& tsk : mod->tasks) {
                tsk->set_debug(debug);
                tsk->set_debug_limit(16); // Only print first 16 bytes
                tsk->set_stats(print_stats);

                // If not debugging, enable fast mode (skips some checks)
                if (!debug && !print_stats)
                    tsk->set_fast(true);
            }
        }
    };

    configure_tasks(seq_tx);
    configure_tasks(seq_rx);

    // -------------------------------------------------------------------------
    // 5. Execution
    // -------------------------------------------------------------------------

    std::cout << "\n[Starting Transmission]..." << std::endl;

    std::atomic<size_t> counter_tx(0);
    std::atomic<size_t> counter_rx(0);

    // TX Thread
    std::thread tx_thread([&]() {
        seq_tx.exec([&]() { return ++counter_tx >= n_frames; });
    });

    // RX (Main Thread)
    seq_rx.exec([&]() { return ++counter_rx >= n_frames; });

    if (tx_thread.joinable())
        tx_thread.join();

    std::cout << "[Transmission Finished]" << std::endl;
    std::cout << "RX Cycles: " << counter_rx << "/" << n_frames << std::endl;

    // -------------------------------------------------------------------------
    // 6. Verification
    // -------------------------------------------------------------------------

    bool valid = true;
    if (counter_rx < n_frames) {
        std::cerr << "FAILURE: Sequence stopped early." << std::endl;
        valid = false;
    }

    if (valid) {
        auto final_data = finalizer.get_final_data();
        if (final_data.empty()) valid = false;
        else {
            const auto& frame = final_data[0];
            for (size_t i = 0; i < frame.size(); ++i) {
                if (frame[i] != (uint8_t)i) {
                    std::cerr << "Mismatch at byte " << i << std::endl;
                    valid = false;
                    break;
                }
            }
        }
    }

    if (valid)
        std::cout << "SUCCESS: Data verified." << std::endl;
    else
        std::cout << "FAILURE: Data corruption or loss." << std::endl;

    // -------------------------------------------------------------------------
    // 7. Statistics Display
    // -------------------------------------------------------------------------

    if (print_stats) {
        std::cout << "\n#########################################" << std::endl;
        std::cout << "# TX STATISTICS                         #" << std::endl;
        std::cout << "#########################################" << std::endl;
        tools::Stats::show(seq_tx.get_modules_per_types(), true, false);

        std::cout << "\n#########################################" << std::endl;
        std::cout << "# RX STATISTICS                         #" << std::endl;
        std::cout << "#########################################" << std::endl;
        tools::Stats::show(seq_rx.get_modules_per_types(), true, false);
    }

    return valid ? 0 : 1;
}