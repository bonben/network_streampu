#include <iostream>
#include <vector>
#include <cassert>
#include <cstring>
#include <iomanip> // For std::setw

#include "UdpReassembler.hpp"

// --------------------------------------------------------------------------
// TEST UTILS
// --------------------------------------------------------------------------

#define ASSERT_TRUE(condition, msg) \
    if (!(condition)) { \
        std::cerr << "[FAIL] " << msg << std::endl; \
        std::exit(1); \
    } else { \
        std::cout << "[PASS] " << msg << std::endl; \
    }

// Helper to generate a fake UDP packet payload consistent with the protocol
struct FakePacket {
    StreampuHeader header;
    std::vector<uint8_t> payload;
};

FakePacket create_packet(uint32_t frame_id, uint16_t index, uint16_t total, uint8_t fill_val) {
    FakePacket p;
    p.header.frame_id = frame_id;
    p.header.frag_index = index;
    p.header.total_frags = total;

    // Fill payload with a recognizable pattern
    // Note: We use the max payload size to simulate a full intermediate packet
    p.payload.resize(STREAMPU_MAX_PAYLOAD, fill_val);

    return p;
}

// --------------------------------------------------------------------------
// TEST CASES
// --------------------------------------------------------------------------

void test_nominal_ordered() {
    std::cout << "\n--- TEST: Nominal Case (Ordered) ---" << std::endl;
    UdpReassembler reassembler;

    // Frame 100: 3 fragments
    // Packet 0
    auto p0 = create_packet(100, 0, 3, 0xAA);
    auto res0 = reassembler.add_fragment(p0.header, p0.payload.data(), p0.payload.size());
    ASSERT_TRUE(!res0.complete, "Packet 0/3 should not complete frame");

    // Packet 1
    auto p1 = create_packet(100, 1, 3, 0xBB);
    auto res1 = reassembler.add_fragment(p1.header, p1.payload.data(), p1.payload.size());
    ASSERT_TRUE(!res1.complete, "Packet 1/3 should not complete frame");

    // Packet 2 (Final)
    auto p2 = create_packet(100, 2, 3, 0xCC);
    auto res2 = reassembler.add_fragment(p2.header, p2.payload.data(), p2.payload.size());
    ASSERT_TRUE(res2.complete, "Packet 2/3 SHOULD complete frame");

    // Verification of Reassembled Data
    // We expect: [1400 bytes of AA] [1400 bytes of BB] [1400 bytes of CC]
    size_t expected_size = 3 * STREAMPU_MAX_PAYLOAD;
    ASSERT_TRUE(res2.data.size() == expected_size, "Reassembled size match");

    // Check content at specific offsets
    ASSERT_TRUE(res2.data[0] == 0xAA, "Check first chunk content");
    ASSERT_TRUE(res2.data[STREAMPU_MAX_PAYLOAD] == 0xBB, "Check second chunk content");
    ASSERT_TRUE(res2.data[STREAMPU_MAX_PAYLOAD * 2] == 0xCC, "Check third chunk content");
}

void test_out_of_order() {
    std::cout << "\n--- TEST: Out-of-Order Case ---" << std::endl;
    UdpReassembler reassembler;

    // Frame 200: 3 fragments. Arrival order: 2 -> 0 -> 1

    // 1. Receive Last Packet (#2) first
    auto p2 = create_packet(200, 2, 3, 0x22);
    auto res2 = reassembler.add_fragment(p2.header, p2.payload.data(), p2.payload.size());
    ASSERT_TRUE(!res2.complete, "Packet 2/3 (arrived 1st) not complete");

    // 2. Receive First Packet (#0) second
    auto p0 = create_packet(200, 0, 3, 0x00);
    auto res0 = reassembler.add_fragment(p0.header, p0.payload.data(), p0.payload.size());
    ASSERT_TRUE(!res0.complete, "Packet 0/3 (arrived 2nd) not complete");

    // 3. Receive Middle Packet (#1) last
    auto p1 = create_packet(200, 1, 3, 0x11);
    auto res1 = reassembler.add_fragment(p1.header, p1.payload.data(), p1.payload.size());
    ASSERT_TRUE(res1.complete, "Packet 1/3 (arrived last) completes frame");

    // Verify integrity (Offset check)
    // Data should be [00...][11...][22...] despite arrival order
    ASSERT_TRUE(res1.data[0] == 0x00, "Start byte is 0x00");
    ASSERT_TRUE(res1.data[STREAMPU_MAX_PAYLOAD * 2] == 0x22, "End byte is 0x22");
}

void test_duplicate_packets() {
    std::cout << "\n--- TEST: Duplicate Packets ---" << std::endl;
    UdpReassembler reassembler;

    // Frame 300: 2 fragments. Packet 0 arrives twice.
    auto p0 = create_packet(300, 0, 2, 0xAA);

    reassembler.add_fragment(p0.header, p0.payload.data(), p0.payload.size());
    auto res_dup = reassembler.add_fragment(p0.header, p0.payload.data(), p0.payload.size());

    ASSERT_TRUE(!res_dup.complete, "Duplicate packet should be ignored");

    auto p1 = create_packet(300, 1, 2, 0xBB);
    auto res_final = reassembler.add_fragment(p1.header, p1.payload.data(), p1.payload.size());

    ASSERT_TRUE(res_final.complete, "Final packet completes frame despite duplicates");
    ASSERT_TRUE(res_final.data.size() == 2 * STREAMPU_MAX_PAYLOAD, "Size is correct");
}

void test_interleaved_frames() {
    std::cout << "\n--- TEST: Interleaved Frames (Multiplexing) ---" << std::endl;
    UdpReassembler reassembler;

    // Simulating two streams arriving at the same time: Frame A (ID 10) and Frame B (ID 20)
    auto a0 = create_packet(10, 0, 2, 0xAA);
    auto b0 = create_packet(20, 0, 2, 0xBB);
    auto a1 = create_packet(10, 1, 2, 0xAA);
    auto b1 = create_packet(20, 1, 2, 0xBB);

    // Sequence: A0 -> B0 -> A1 (Finish A) -> B1 (Finish B)
    reassembler.add_fragment(a0.header, a0.payload.data(), a0.payload.size()); // A pending
    reassembler.add_fragment(b0.header, b0.payload.data(), b0.payload.size()); // B pending

    auto resA = reassembler.add_fragment(a1.header, a1.payload.data(), a1.payload.size());
    ASSERT_TRUE(resA.complete, "Frame A finished interleaved");
    ASSERT_TRUE(resA.frame_id == 10, "Finished ID is 10");

    auto resB = reassembler.add_fragment(b1.header, b1.payload.data(), b1.payload.size());
    ASSERT_TRUE(resB.complete, "Frame B finished interleaved");
    ASSERT_TRUE(resB.frame_id == 20, "Finished ID is 20");
}

int main() {
    test_nominal_ordered();
    test_out_of_order();
    test_duplicate_packets();
    test_interleaved_frames();

    std::cout << "\n[ALL TESTS PASSED]" << std::endl;
    return 0;
}