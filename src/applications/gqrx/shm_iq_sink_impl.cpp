#include "shm_iq_sink_impl.h"
#include "shm_ringbuf.hpp"
#include <iostream>
#include <cstring>
#include <algorithm>
#include <ctime>

shm_iq_sink_impl::shm_iq_sink_impl(unsigned long sample_rate)
    : gr::sync_block("shm_iq_sink",
                     gr::io_signature::make(1, 2, sizeof(gr_complex)),
                     gr::io_signature::make(0, 0, 0)),
      last_overflow_warn_(0)
{
    try {
        /* 32 MB ring buffer; 2 bytes per CU8 IQ pair */
        ringbuf_ = std::make_unique<ShmRingbuf>(32 * 1024 * 1024, sample_rate, 2);

        std::cout << "\n================================================\n"
                  << "GNU Radio Shared Memory IQ Sink initialized\n"
                  << "  Sample rate : " << sample_rate << " Hz\n"
                  << "  Transport   : memfd_create(\"" MEMFD_RINGBUF_NAME "\")"
                     " + eventfd\n"
                  << "  Sample fmt  : CU8 (interleaved uint8 I/Q, "
                     "value = sample * 127.5 + 127.5)\n"
                  << "\nConsumer fd discovery:\n"
                  << "  Child process  : set " GQRX_IQ_MEMFD_ENV "=<fd> "
                     GQRX_IQ_EVENTFD_ENV "=<fd> before exec\n"
                  << "  Unrelated proc : connect to Unix socket "
                     GQRX_IQ_SOCK_DEFAULT " (or $" GQRX_IQ_SOCK_ENV ")\n"
                  << "================================================\n"
                  << std::endl;
    } catch (const std::exception &e) {
        std::cerr << "Failed to create memfd ring buffer: " << e.what() << std::endl;
        throw;
    }
}

shm_iq_sink_impl::~shm_iq_sink_impl()
{
    ringbuf_.reset();
}

int shm_iq_sink_impl::work(int noutput_items,
                            gr_vector_const_void_star& input_items,
                            gr_vector_void_star& output_items)
{
    const gr_complex *in = (const gr_complex *)input_items[0];

    /* Ensure batch buffer is large enough (2 bytes per IQ pair) */
    const size_t needed = (size_t)noutput_items * 2;
    if (batch_buf_.size() < needed)
        batch_buf_.resize(needed);

    /* Batch-convert entire block to CU8.
     * Standard symmetric centering: value * 127.5 + 127.5
     *   full-scale +1.0 → 255, zero carrier → 127.5 (rounds to 127 or 128),
     *   full-scale -1.0 → 0.
     * This matches RTL-SDR hardware output and rtl_433 CU8 expectations. */
    for (int i = 0; i < noutput_items; i++) {
        float iv = std::max(-1.0f, std::min(1.0f, in[i].real()));
        float qv = std::max(-1.0f, std::min(1.0f, in[i].imag()));
        batch_buf_[2 * i]     = (uint8_t)(iv * 127.5f + 127.5f);
        batch_buf_[2 * i + 1] = (uint8_t)(qv * 127.5f + 127.5f);
    }

    /* Single write per work() call */
    ssize_t written = ringbuf_->write(batch_buf_.data(), needed);
    if (written < 0) {
        /* Ring buffer full — non-fatal overflow: drop this block and emit a
         * rate-limited warning (at most once per second). */
        time_t now = time(nullptr);
        if (now != last_overflow_warn_) {
            std::cerr << "[SHM] Ring buffer full, dropping block of "
                      << noutput_items << " samples" << std::endl;
            last_overflow_warn_ = now;
        }
    }

    /* Always acknowledge consumption to keep GNU Radio's scheduler happy */
    return noutput_items;
}
