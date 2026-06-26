#ifndef SHM_RINGBUF_H
#define SHM_RINGBUF_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdio.h>

#define SHM_RINGBUF_NAME "/gqrx_iq_ringbuf"
#define SHM_RINGBUF_MAGIC 0x52494E47  /* 'RING' */
#define SHM_RINGBUF_VERSION 1

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint64_t head;
    uint64_t tail;
    size_t bufsize;
    uint32_t sample_rate;
    uint32_t sample_size;  /* bytes per sample (2 for CU8, 4 for CS16) */
} shm_ringbuf_header_t;

typedef struct {
    int shm_fd;
    void *region;
    size_t region_size;
    shm_ringbuf_header_t *hdr;
    uint8_t *buffer;
} shm_ringbuf_t;

/**
 * Create and initialize a new shared memory ring buffer (producer side)
 * @param bufsize Size of the ring buffer (excluding header)
 * @param sample_rate Sample rate in Hz
 * @param sample_size Bytes per sample (2 for CU8, 4 for CS16)
 * @param out_ctx Output context
 * @return 0 on success, -1 on error
 */
int shm_ringbuf_create(size_t bufsize, uint32_t sample_rate, uint32_t sample_size, shm_ringbuf_t *out_ctx);

/**
 * Open an existing shared memory ring buffer (consumer side)
 * @param out_ctx Output context
 * @return 0 on success, -1 on error
 */
int shm_ringbuf_open(shm_ringbuf_t *out_ctx);

/**
 * Close a ring buffer
 * @param ctx Ring buffer context
 * @return 0 on success, -1 on error
 */
int shm_ringbuf_close(shm_ringbuf_t *ctx);

/**
 * Destroy the shared memory (unlink it)
 * Only call this once, typically on producer shutdown
 */
void shm_ringbuf_destroy(void);

/**
 * Get available bytes to read
 */
size_t shm_ringbuf_available(const shm_ringbuf_t *ctx);

/**
 * Get free space for writing
 */
size_t shm_ringbuf_free(const shm_ringbuf_t *ctx);

/**
 * Write data to the ring buffer
 * @return Number of bytes written, -1 on error
 */
ssize_t shm_ringbuf_write(shm_ringbuf_t *ctx, const void *data, size_t len);

/**
 * Read data from the ring buffer
 * @param timeout_ms Timeout in milliseconds (-1 = blocking, 0 = non-blocking)
 * @return Number of bytes read, 0 if no data, -1 on error
 */
ssize_t shm_ringbuf_read(shm_ringbuf_t *ctx, void *out_buf, size_t max_len, int timeout_ms);

#endif /* SHM_RINGBUF_H */
