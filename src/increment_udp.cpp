/**
 * @file increment_udp.cpp
 * @brief Hardware-in-the-Loop verification.
 * Uses Task Binding to link TX and RX in a single sequence.
 */

#include <iostream>
#include <vector>
#include <string>
#include <atomic>
#include <sstream>
#include <iomanip> // Added for std::hex, std::setw
#include <getopt.h>

#include <streampu.hpp>

#include "Sink_UDP.hpp"
#include "Source_UDP.hpp"

using namespace spu;
using namespace spu::module;
using namespace spu::runtime;

int main(int argc, char** argv)
{
    // -------------------------------------------------------------------------
    // 0. Configuration
    // -------------------------------------------------------------------------
    std::string ip = "127.0.0.1";
    int tx_port = 9998; // To reflector (socat)
    int rx_port = 9999; // Local listen
    size_t n_frames = 10;
    size_t data_size = 3;
    bool print_stats = false;
    bool debug = false;

    int opt;
    while ((opt = getopt(argc, argv, "n:sgh")) != -1) {
        switch (opt) {
            case 'n': n_frames = std::stoi(optarg); break;
            case 's': print_stats = true; break;
            case 'g': debug = true; break;
            default: break;
        }
    }

    std::cout << "--- HIL Verification ---" << std::endl;
    std::cout << "TX Port: " << tx_port << " -> RX Port: " << rx_port << std::endl;

    // -------------------------------------------------------------------------
    // 1. Modules
    // -------------------------------------------------------------------------

    Source_random<uint8_t> source(data_size);
    source.set_seed(0);

    Incrementer<uint8_t> ref_inc(data_size);

    Sink_UDP<uint8_t>   dut_sink(data_size, ip, tx_port);
    Source_UDP<uint8_t> dut_source(data_size, rx_port);

    Stateless comparator;
    comparator.set_name("comparator");
    auto& task_comp = comparator.create_task("compare");
    auto sock_ref = comparator.create_socket_in<uint8_t>(task_comp, "ref_in", data_size);
    auto sock_dut = comparator.create_socket_in<uint8_t>(task_comp, "dut_in", data_size);

    // -------------------------------------------------------------------------
    // COMPARATOR LOGIC (Updated)
    // -------------------------------------------------------------------------
    comparator.create_codelet(task_comp,
        [sock_ref, sock_dut, data_size](Module& m, runtime::Task& t, const size_t frame_id) -> int {
            auto tab_ref = t[sock_ref].get_dataptr<const uint8_t>();
            auto tab_dut = t[sock_dut].get_dataptr<const uint8_t>();

            for (size_t i = 0; i < data_size; i++) {
                if (tab_ref[i] != tab_dut[i]) {
                    std::stringstream ss;
                    ss << "\n[Verification Fail] Frame " << frame_id << " mismatch at byte " << i << "!\n"
                       << "Expected (Ref): 0x" << std::hex << +tab_ref[i] << "\n"
                       << "Received (DUT): 0x" << std::hex << +tab_dut[i] << "\n\n";

                    // --- FULL BUFFER DUMP ---

                    auto dump_buf = [&](const char* name, const uint8_t* buf) {
                        ss << ">>> " << name << " CONTENT (" << std::dec << data_size << " bytes):";
                        for (size_t j = 0; j < data_size; ++j) {
                            if (j % 16 == 0) ss << "\n" << std::setw(4) << std::setfill('0') << std::hex << j << ": ";

                            // Highlight the specific error byte with markers
                            if (j == i) ss << "!!" << std::setw(2) << +buf[j] << "!! ";
                            else        ss << "  " << std::setw(2) << +buf[j] << "   ";
                        }
                        ss << "\n\n";
                    };

                    dump_buf("REFERENCE", tab_ref);
                    dump_buf("DUT (UDP)", tab_dut);

                    std::cout << ss.str();
                    // throw tools::runtime_error(__FILE__, __LINE__, __func__, ss.str());
                }
            }
            return status_t::SUCCESS;
        });

    // -------------------------------------------------------------------------
    // 2. Binding
    // -------------------------------------------------------------------------

    ref_inc["increment::in"]      = source["generate::out_data"];
    comparator["compare::ref_in"] = ref_inc["increment::out"];

    dut_sink["send::in_data"]     = source["generate::out_data"];
    comparator["compare::dut_in"] = dut_source["generate::out_data"];

    // -------------------------------------------------------------------------
    // 3. Control Dependency (RX depends on TX)
    // -------------------------------------------------------------------------
    dut_source("generate") = dut_sink("send");

    // -------------------------------------------------------------------------
    // 4. Sequence
    // -------------------------------------------------------------------------
    Sequence sequence(source("generate"), 1); // Force 1 thread

    for (auto& mod : sequence.get_modules<Module>(false)) {
        for (auto& tsk : mod->tasks) {
            tsk->set_stats(print_stats);
            tsk->set_debug(debug);
            if (!print_stats && !debug) tsk->set_fast(true);
        }
    }

    // -------------------------------------------------------------------------
    // 5. Execution
    // -------------------------------------------------------------------------
    std::cout << "Running verification (" << n_frames << " frames)..." << std::endl;

    std::atomic<size_t> cnt(0);
    try {
        sequence.exec([&]() { return ++cnt >= n_frames; });
        std::cout << "SUCCESS: Verification Passed!" << std::endl;
    } catch (const std::exception& e) {
        // Just print, don't re-throw, so we exit cleanly
        std::cerr << e.what() << std::endl;
        return 1;
    }

    if (print_stats) tools::Stats::show(sequence.get_modules_per_types(), true, false);

    return 0;
}