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
        // Cloning is strictly forbidden for this class.
        std::cerr << "Fatal: cloning Sink_UDP is not allowed." << std::endl;
        std::terminate();
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