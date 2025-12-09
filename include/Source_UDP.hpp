/**
 * @file Source_UDP.hpp
 * @brief StreamPU Source wrapper for UdpSource.
 */
#ifndef SOURCE_UDP_HPP_
#define SOURCE_UDP_HPP_

#include <vector>
#include <cstdint>
#include <iostream>
#include <streampu.hpp>

// Include your existing logic (assumed to be in the include path)
#include "UdpSource.hpp"

namespace spu
{
namespace module
{

template <typename B = uint8_t>
class Source_UDP : public Source<B>
{
protected:
    UdpSource udp_source_;
    int timeout_ms_;

public:
    Source_UDP(const int max_data_size, const int port, int timeout_ms = 1000)
    : Source<B>(max_data_size),
      udp_source_(port),
      timeout_ms_(timeout_ms)
    {
        const std::string name = "Source_UDP";
        this->set_name(name);
        this->set_short_name(name);

        // Start the internal receiving thread of UdpSource
        udp_source_.start();
    }

    virtual ~Source_UDP()
    {
        udp_source_.stop();
    }

    virtual Source_UDP<B>* clone() const
    {
        // Cloning network sockets is tricky.
        // For parallel execution, StreamPU clones modules.
        // A simple clone would try to bind the same port twice, which fails.
        // In this specific context, deep cloning needs careful architecture (e.g., shared socket).
        // For this basic test, we throw to warn if someone tries multithreaded source.
        throw tools::runtime_error(__FILE__, __LINE__, __func__, "Cloning Source_UDP is not supported in this demo.");
    }

protected:
    void _generate(B *out_data, const size_t frame_id) override
    {
        // Blocking call to get a frame
        std::vector<uint8_t> received_data = udp_source_.pop_frame(timeout_ms_);

        if (received_data.empty())
        {
            // Handle timeout or empty data
            // For now, fill with zeros or keep previous data to avoid undefined behavior
             std::fill_n(out_data, this->max_data_size, 0);
        }
        else
        {
            // Copy received data to StreamPU buffer
            size_t copy_size = std::min((size_t)this->max_data_size, received_data.size());
            std::copy_n(received_data.begin(), copy_size, out_data);

            // Zero padding if received data is smaller than task buffer
            if (copy_size < (size_t)this->max_data_size) {
                 std::fill_n(out_data + copy_size, this->max_data_size - copy_size, 0);
            }
        }
    }
};

}
}

#endif // SOURCE_UDP_HPP_