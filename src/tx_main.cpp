/**
 * @file tx_main.cpp
 * @brief StreamPU UDP Transmitter Application.
 */

#include <iostream>
#include <vector>
#include <string>
#include <atomic>
#include <numeric>
#include <getopt.h>

#include <streampu.hpp>
#include "Sink_UDP.hpp"

using namespace spu;
using namespace spu::module;
using namespace spu::runtime;

void print_help(char** argv) {
    std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
    std::cout << "  -i, --ip              Destination IP [127.0.0.1]" << std::endl;
    std::cout << "  -p, --port            Destination port [9999]" << std::endl;
    std::cout << "  -n, --n-frames        Number of frames to send [100]" << std::endl;
    std::cout << "  -d, --data-size       Size of data in bytes [2048]" << std::endl;
    std::cout << "  -s, --stats           Enable statistics [false]" << std::endl;
    std::cout << "  -h, --help            Show this help" << std::endl;
}

int main(int argc, char** argv)
{
    std::string ip = "127.0.0.1";
    int port = 9999;
    size_t n_frames = 100;
    size_t data_size = 2048;
    bool print_stats = false;

    struct option longopts[] = {
        { "ip",          required_argument, NULL, 'i' },
        { "port",        required_argument, NULL, 'p' },
        { "n-frames",    required_argument, NULL, 'n' },
        { "data-size",   required_argument, NULL, 'd' },
        { "stats",       no_argument,       NULL, 's' },
        { "help",        no_argument,       NULL, 'h' },
        { NULL,          0,                 NULL, 0   }
    };

    while (true) {
        const int opt = getopt_long(argc, argv, "i:p:n:d:sh", longopts, 0);
        if (opt == -1) break;
        switch (opt) {
            case 'i': ip = std::string(optarg); break;
            case 'p': port = std::stoi(optarg); break;
            case 'n': n_frames = std::stoi(optarg); break;
            case 'd': data_size = std::stoi(optarg); break;
            case 's': print_stats = true; break;
            case 'h': print_help(argv); return 0;
            default: break;
        }
    }

    std::cout << "--- TX Configuration ---" << std::endl;
    std::cout << "Target:     " << ip << ":" << port << std::endl;
    std::cout << "Frames:     " << n_frames << std::endl;
    std::cout << "Data Size:  " << data_size << std::endl;
    std::cout << "Stats:      " << (print_stats ? "ON" : "OFF") << std::endl;
    std::cout << "------------------------" << std::endl;

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

    // Task Config
    for (auto& mod : seq_tx.get_modules<Module>(false)) {
        for (auto& tsk : mod->tasks) {
            tsk->set_stats(print_stats);
            if (!print_stats) tsk->set_fast(true);
        }
    }

    std::cout << "[TX] Sending..." << std::endl;

    std::atomic<size_t> counter_tx(0);
    seq_tx.exec([&]() { return ++counter_tx >= n_frames; });

    std::cout << "[TX] Finished." << std::endl;

    if (print_stats) {
        std::cout << "\n--- TX Statistics ---" << std::endl;
        tools::Stats::show(seq_tx.get_modules_per_types(), true, false);
    }

    return 0;
}
