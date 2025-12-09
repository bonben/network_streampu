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
 * - Streampu Header:     12 bytes (now 12 bytes with 32-bit fields)
 * = Theoretical Max:     1460 bytes.
 * * We set it to 1400 to provide a safety margin for:
 * - VLAN tags (4 bytes)
 * - Tunnels (VPN/GRE overhead)
 * - PPPoE encapsulation
 */
static const uint32_t STREAMPU_MAX_PAYLOAD = 1400;

/**
 * @brief Maximum supported frame size.
 * * Limited by the 'total_frags' field (uint32_t).
 * 4,294,967,295 fragments * 1400 bytes ~= 5.9 TB.
 */
static const uint64_t STREAMPU_MAX_FRAME_SIZE = 4294967295UL * STREAMPU_MAX_PAYLOAD;


// --------------------------------------------------------------------------
// BINARY HEADER STRUCTURE (12 BYTES)
// --------------------------------------------------------------------------

// Force 1-byte packing to prevent the compiler from adding padding.
// The structure must remain exactly 12 bytes (96 bits) for consistent alignment.
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
     * @brief Fragment Index (32 bits).
     * Current sequence number of this packet within the frame.
     * Range: [0 ... total_frags-1]
     * * @note Used to calculate memory offset: address = frag_index * STREAMPU_MAX_PAYLOAD
     */
    uint32_t frag_index;

    /**
     * @brief Total Fragments (32 bits).
     * Total number of packets expected for this frame.
     * * @note Convention: Little Endian.
     */
    uint32_t total_frags;
};

#pragma pack(pop) // Restore default packing

// --------------------------------------------------------------------------
// STATIC VALIDATION
// --------------------------------------------------------------------------

// Ensure at compile-time that the struct size is exactly 12 bytes.
static_assert(sizeof(StreampuHeader) == 12,
    "Streampu Protocol Error: Header size mismatch! Must be exactly 12 bytes.");

#endif // STREAMPU_PROTOCOL_H