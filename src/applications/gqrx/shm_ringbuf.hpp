#ifndef SHM_RINGBUF_HPP
#define SHM_RINGBUF_HPP

#include <cstddef>
#include <cstdint>
#include <memory>

extern "C" {
#include "shm_ringbuf.h"
}

class ShmRingbuf {
public:
    /**
     * Create a new shared memory ring buffer (producer)
     * @param bufsize_bytes Size of the ring buffer in bytes
     * @param sample_rate Sample rate in Hz
     * @param sample_size Bytes per sample (2 for CU8, 4 for CS16)
     */
    ShmRingbuf(size_t bufsize_bytes, uint32_t sample_rate, uint32_t sample_size);
    
    /**
     * Open an existing shared memory ring buffer (consumer)
     */
    ShmRingbuf();

    ~ShmRingbuf();

    /**
     * Write data to the ring buffer
     * @return Number of bytes written, -1 on error
     */
    ssize_t write(const void *data, size_t len);

    /**
     * Read data from the ring buffer
     * @return Number of bytes read, 0 if no data, -1 on error
     */
    ssize_t read(void *out_buf, size_t max_len, int timeout_ms = -1);

    size_t available() const;
    size_t free_space() const;

    uint32_t get_sample_rate() const { return ctx_.hdr->sample_rate; }
    uint32_t get_sample_size() const { return ctx_.hdr->sample_size; }

private:
    shm_ringbuf_t ctx_;
};

#endif /* SHM_RINGBUF_HPP */
