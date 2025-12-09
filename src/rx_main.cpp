/**
 * @file rx_main.cpp
 * @brief StreamPU UDP Receiver Application.
 */

#include <iostream>
#include <vector>
#include <string>
#include <atomic>
#include <getopt.h>

#include <streampu.hpp>
#include "Source_UDP.hpp"

using namespace spu;
using namespace spu::module;
using namespace spu::runtime;

void print_help(char** argv) {
    std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
    std::cout << "  -p, --port            Local listening port [9999]" << std::endl;
    std::cout << "  -n, --n-frames        Number of frames to expect [100]" << std::endl;
    std::cout << "  -d, --data-size       Size of data in bytes [2048]" << std::endl;
    std::cout << "  -s, --stats           Enable statistics [false]" << std::endl;
    std::cout << "  -h, --help            Show this help" << std::endl;
}

int main(int argc, char** argv)
{
    int port = 9999;
    size_t n_frames = 100;
    size_t data_size = 2048;
    bool print_stats = false;

    struct option longopts[] = {
        { "port",        required_argument, NULL, 'p' },
        { "n-frames",    required_argument, NULL, 'n' },
        { "data-size",   required_argument, NULL, 'd' },
        { "stats",       no_argument,       NULL, 's' },
        { "help",        no_argument,       NULL, 'h' },
        { NULL,          0,                 NULL, 0   }
    };

    while (true) {
        const int opt = getopt_long(argc, argv, "p:n:d:sh", longopts, 0);
        if (opt == -1) break;
        switch (opt) {
            case 'p': port = std::stoi(optarg); break;
            case 'n': n_frames = std::stoi(optarg); break;
            case 'd': data_size = std::stoi(optarg); break;
            case 's': print_stats = true; break;
            case 'h': print_help(argv); return 0;
            default: break;
        }
    }

    std::cout << "--- RX Configuration ---" << std::endl;
    std::cout << "Port:       " << port << std::endl;
    std::cout << "Frames:     " << n_frames << std::endl;
    std::cout << "Data Size:  " << data_size << std::endl;
    std::cout << "Stats:      " << (print_stats ? "ON" : "OFF") << std::endl;
    std::cout << "------------------------" << std::endl;

    // Modules
    Source_UDP<uint8_t> udp_source(data_size, port);
    Finalizer<uint8_t>  finalizer(data_size);

    // Binding
    finalizer["finalize::in"] = udp_source["generate::out_data"];

    // Sequence
    Sequence seq_rx(udp_source("generate"));

    // Task Config
    for (auto& mod : seq_rx.get_modules<Module>(false)) {
        for (auto& tsk : mod->tasks) {
            tsk->set_stats(print_stats);
            if (!print_stats) tsk->set_fast(true);
        }
    }

    std::cout << "[RX] Listening..." << std::endl;

    std::atomic<size_t> counter_rx(0);
    seq_rx.exec([&]() { return ++counter_rx >= n_frames; });

    std::cout << "[RX] Reception Finished." << std::endl;

    // Verification
    bool valid = true;
    auto final_data = finalizer.get_final_data();

    if (final_data.empty()) {
        std::cerr << "FAILURE: No data received." << std::endl;
        valid = false;
    } else {
        const auto& frame = final_data[0];
        // Only verify the last frame to save time, assuming pattern 0,1,2...
        for (size_t i = 0; i < frame.size(); ++i) {
            if (frame[i] != (uint8_t)i) {
                std::cerr << "Mismatch at byte " << i
                          << " (Expected " << (int)((uint8_t)i)
                          << ", Got " << (int)frame[i] << ")" << std::endl;
                valid = false;
                break;
            }
        }
    }

    if (valid)
        std::cout << "SUCCESS: Last frame verified." << std::endl;
    else
        std::cout << "FAILURE: Data corruption detected." << std::endl;

    if (print_stats) {
        std::cout << "\n--- RX Statistics ---" << std::endl;
        tools::Stats::show(seq_rx.get_modules_per_types(), true, false);
    }

    return valid ? 0 : 1;
}
