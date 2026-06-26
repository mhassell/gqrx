#include "shm_iq_sink_impl.h"
#include "shm_ringbuf.hpp"
#include <iostream>
#include <cstring>
#include <algorithm>

shm_iq_sink_impl::shm_iq_sink_impl(unsigned long sample_rate)
    : gr::sync_block("shm_iq_sink",
                     gr::io_signature::make(1, 2, sizeof(gr_complex)),
                     gr::io_signature::make(0, 0, 0))
{
    try {
        /* Create the shared memory ring buffer
           32MB buffer, 2 bytes per CU8 sample, at specified sample rate */
        ringbuf_ = std::make_unique<ShmRingbuf>(32 * 1024 * 1024, sample_rate, 2);
        
        std::cout << "\n================================================\n"
                  << "GNU Radio Shared Memory IQ Sink initialized\n"
                  << "Sample rate: " << sample_rate << " Hz\n"
                  << "\nStart rtl_433 with:\n"
                  << "  ./src/rtl_433 -r shm:// -f 433970000 -v -s 1800000\n"
                  << "================================================\n" << std::endl;
    } catch (const std::exception &e) {
        std::cerr << "Failed to create shared memory ring buffer: " << e.what() << std::endl;
        throw;
    }
}

shm_iq_sink_impl::~shm_iq_sink_impl()
{
    ringbuf_.reset();
    shm_ringbuf_destroy();
}

int shm_iq_sink_impl::work(int noutput_items,
                            gr_vector_const_void_star& input_items,
                            gr_vector_void_star& output_items)
{
    const gr_complex *in = (const gr_complex *)input_items[0];
    
    /* Convert complex to CU8 and write to ring buffer */
    for (int i = 0; i < noutput_items; i++) {
        uint8_t cu8[2];
        /* Scale from [-1, 1] to [0, 255] with proper centering */
        cu8[0] = (uint8_t)(std::max(-1.0, std::min(1.0, in[i].real())) * 127.0 + 128.0);
        cu8[1] = (uint8_t)(std::max(-1.0, std::min(1.0, in[i].imag())) * 127.0 + 128.0);
        
        if (ringbuf_->write(cu8, 2) < 0) {
            std::cerr << "Ring buffer write failed" << std::endl;
            return -1;
        }
    }
    
    return noutput_items;
}
