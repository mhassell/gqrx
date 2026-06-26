#include "shm_ringbuf.hpp"
#include <iostream>
#include <stdexcept>

ShmRingbuf::ShmRingbuf(size_t bufsize_bytes, uint32_t sample_rate, uint32_t sample_size)
{
    if (shm_ringbuf_create(bufsize_bytes, sample_rate, sample_size, &ctx_) < 0) {
        throw std::runtime_error("Failed to create shared memory ring buffer");
    }
}

ShmRingbuf::ShmRingbuf()
{
    if (shm_ringbuf_open(&ctx_) < 0) {
        throw std::runtime_error("Failed to open shared memory ring buffer");
    }
}

ShmRingbuf::~ShmRingbuf()
{
    if (ctx_.region) {
        shm_ringbuf_close(&ctx_);
    }
}

ssize_t ShmRingbuf::write(const void *data, size_t len)
{
    return shm_ringbuf_write(&ctx_, data, len);
}

ssize_t ShmRingbuf::read(void *out_buf, size_t max_len, int timeout_ms)
{
    return shm_ringbuf_read(&ctx_, out_buf, max_len, timeout_ms);
}

size_t ShmRingbuf::available() const
{
    return shm_ringbuf_available(&ctx_);
}

size_t ShmRingbuf::free_space() const
{
    return shm_ringbuf_free(&ctx_);
}
