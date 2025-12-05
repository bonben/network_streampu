/**
 * @file streampu_protocol.h
 * @brief Network Protocol Definition for Streampu (UDP).
 * * This file defines the binary structure of the header used for fragmentation.
 * It is designed to be compatible with FPGA AXI-Stream interfaces (64-bit alignment).
 */

#ifndef STREAMPU_PROTOCOL_H
#define STREAMPU_PROTOCOL_H

#include <cstdint>

// --------------------------------------------------------------------------
// PROTOCOL CONFIGURATION
// --------------------------------------------------------------------------

/**
 * @brief Maximum payload size per UDP packet (in bytes).
 * * Calculation logic:
 * Standard Ethernet MTU: 1500 bytes
 * - IP Header:           20 bytes (min)
 * - UDP Header:          8 bytes
 * - Streampu Header:     8 bytes
 * = Theoretical Max:     1464 bytes.
 * * We set it to 1400 to provide a safety margin for:
 * - VLAN tags (4 bytes)
 * - Tunnels (VPN/GRE overhead)
 * - PPPoE encapsulation
 */
static const uint32_t STREAMPU_MAX_PAYLOAD = 1400;

/**
 * @brief Maximum supported frame size.
 * * Limited by the 'total_frags' field (uint16_t).
 * 65535 fragments * 1400 bytes ~= 91.7 MB.
 */
static const uint64_t STREAMPU_MAX_FRAME_SIZE = 65535UL * STREAMPU_MAX_PAYLOAD;


// --------------------------------------------------------------------------
// BINARY HEADER STRUCTURE (8 BYTES)
// --------------------------------------------------------------------------

// Force 1-byte packing to prevent the compiler from adding padding.
// The structure must remain exactly 8 bytes to align with 64-bit FPGA buses.
#pragma pack(push, 1)

struct StreampuHeader {
    /**
     * @brief Frame Identifier (32 bits).
     * Incremented for every new frame (image/tensor) sent.
     * Allows the receiver to distinguish between packets of Frame N and Frame N+1.
     * * @note Convention: Little Endian.
     */
    uint32_t frame_id;

    /**
     * @brief Fragment Index (16 bits).
     * Current sequence number of this packet within the frame.
     * Range: [0 ... total_frags-1]
     * * @note Used to calculate memory offset: address = frag_index * STREAMPU_MAX_PAYLOAD
     */
    uint16_t frag_index;

    /**
     * @brief Total Fragments (16 bits).
     * Total number of packets expected for this frame.
     * * @note Convention: Little Endian.
     */
    uint16_t total_frags;
};

#pragma pack(pop) // Restore default packing

// --------------------------------------------------------------------------
// STATIC VALIDATION
// --------------------------------------------------------------------------

// Ensure at compile-time that the struct size is exactly 8 bytes.
static_assert(sizeof(StreampuHeader) == 8,
    "Streampu Protocol Error: Header size mismatch! Must be exactly 8 bytes for FPGA compatibility.");

#endif // STREAMPU_PROTOCOL_H