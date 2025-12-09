/**
 * @file Sink_UDP.hpp
 * @brief StreamPU Sink wrapper for UdpSink.
 */
#ifndef SINK_UDP_HPP_
#define SINK_UDP_HPP_

#include <vector>
#include <cstdint>
#include <string>
#include <streampu.hpp>

// Include your existing logic
#include "UdpSink.hpp"

namespace spu
{
namespace module
{

template <typename B = uint8_t>
class Sink_UDP : public Sink<B>
{
protected:
    UdpSink udp_sink_;

public:
    Sink_UDP(const int max_data_size, const std::string& ip, const int port)
    : Sink<B>(max_data_size),
      udp_sink_(ip, port)
    {
        const std::string name = "Sink_UDP";
        this->set_name(name);
        this->set_short_name(name);
    }

    virtual ~Sink_UDP() = default;

    virtual Sink_UDP<B>* clone() const
    {
        // Similar to Source, cloning a Sink implies multiple threads sending to the same IP/Port.
        // UdpSink seems stateless enough (apart from frame_counter) to maybe allow it,
        // but let's block it for safety in this demo.
        throw tools::runtime_error(__FILE__, __LINE__, __func__, "Cloning Sink_UDP is not supported in this demo.");
    }

protected:
    void _send(const B *in_data, const size_t frame_id) override
    {
        // Directly send the buffer provided by StreamPU
        udp_sink_.send_frame(in_data, this->max_data_size);
    }
};

}
}

#endif // SINK_UDP_HPP_