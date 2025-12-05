#include <iostream>
#include <vector>
#include <numeric> // for std::iota
#include <thread>
#include <chrono>

#include "UdpSink.hpp"
#include "UdpSource.hpp"

// Configuration
const uint16_t PORT = 9999;
const std::string IP = "127.0.0.1";
const size_t FRAME_SIZE =  1024 * 1024; // 1 MB

int main() {
    std::cout << "[Test] Starting Streampu UDP Modules..." << std::endl;

    try {
        // 1. Initialize Source (Server)
        UdpSource source(PORT);
        source.start();
        std::cout << "[Source] Listening on port " << PORT << std::endl;

        // 2. Initialize Sink (Client)
        UdpSink sink(IP, PORT);
        std::cout << "[Sink] Targeting " << IP << ":" << PORT << std::endl;

        // 3. Generate Dummy Data
        std::vector<uint8_t> tx_data(FRAME_SIZE);
        // Fill with pattern: 0, 1, 2...
        std::iota(tx_data.begin(), tx_data.end(), 0);

        // 4. Send Frame
        std::cout << "[Sink] Sending 1 MB frame..." << std::endl;
        sink.send_frame(tx_data.data(), tx_data.size());

        // 5. Receive Frame
        std::cout << "[Source] Waiting for frame..." << std::endl;
        std::vector<uint8_t> rx_data = source.pop_frame(1000);

        // 6. Verification
        std::cout << "[Source] Received " << rx_data.size() << " bytes." << std::endl;

        if (rx_data.size() != tx_data.size()) {
            std::cerr << "[FAIL] Size mismatch!" << std::endl;
            return 1;
        }

        // Check content (memcmp)
        if (std::memcmp(tx_data.data(), rx_data.data(), FRAME_SIZE) == 0) {
            std::cout << "[SUCCESS] Data integrity verified." << std::endl;
        } else {
            std::cerr << "[FAIL] Data corruption detected!" << std::endl;
        }

        source.stop();

    } catch (const std::exception& e) {
        std::cerr << "[ERROR] " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

// #include <iostream>
// #include <vector>
// #include "UdpPacketizer.hpp"

// int main_packetizer() {
//     // 1. Créer une fausse donnée de 1 Mo + 500 octets
//     size_t data_size = 1024 * 1024 + 500;
//     std::vector<uint8_t> my_data(data_size, 0xAB); // Rempli de 0xAB

//     UdpPacketizer packetizer;

//     std::cout << "Préparation de la trame (" << data_size << " bytes)..." << std::endl;

//     try {
//         // Frame ID arbitraire : 42
//         size_t count = packetizer.prepare_frame(my_data.data(), my_data.size(), 42);

//         std::cout << "Fragments générés : " << count << std::endl;

//         // Vérifions le premier et le dernier paquet
//         const auto* packets = packetizer.get_packets();

//         // Premier paquet
//         std::cout << "--- Fragment 0 ---" << std::endl;
//         std::cout << "Header ID: " << packets[0].header.frame_id << std::endl;
//         std::cout << "Index: " << packets[0].header.frag_index << std::endl;
//         std::cout << "Total: " << packets[0].header.total_frags << std::endl;
//         std::cout << "Payload Len: " << packets[0].iov[1].iov_len << " (Attendu: 1400)" << std::endl;

//         // Dernier paquet
//         size_t last = count - 1;
//         std::cout << "--- Fragment " << last << " ---" << std::endl;
//         std::cout << "Payload Len: " << packets[last].iov[1].iov_len << std::endl;

//     } catch (const std::exception& e) {
//         std::cerr << "Erreur : " << e.what() << std::endl;
//     }

//     return 0;
// }