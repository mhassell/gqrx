#include "shm_ringbuf.h"
#include <string.h>
#include <errno.h>
#include <sys/eventfd.h>

int shm_ringbuf_create(size_t bufsize, uint32_t sample_rate, uint32_t sample_size, shm_ringbuf_t *out_ctx)
{
    if (!out_ctx || bufsize == 0) {
        errno = EINVAL;
        return -1;
    }

    memset(out_ctx, 0, sizeof(*out_ctx));

    /* Unlink any existing shared memory (clean slate) */
    shm_unlink(SHM_RINGBUF_NAME);

    /* Create shared memory object */
    int shm_fd = shm_open(SHM_RINGBUF_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0) {
        perror("shm_open");
        return -1;
    }

    size_t total_size = sizeof(shm_ringbuf_header_t) + bufsize;

    /* Set the size */
    if (ftruncate(shm_fd, total_size) < 0) {
        perror("ftruncate");
        close(shm_fd);
        shm_unlink(SHM_RINGBUF_NAME);
        return -1;
    }

    /* Map the shared memory */
    void *region = mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (region == MAP_FAILED) {
        perror("mmap");
        close(shm_fd);
        shm_unlink(SHM_RINGBUF_NAME);
        return -1;
    }

    /* Initialize the context */
    out_ctx->shm_fd = shm_fd;
    out_ctx->region = region;
    out_ctx->region_size = total_size;
    out_ctx->hdr = (shm_ringbuf_header_t *)region;
    out_ctx->buffer = (uint8_t *)region + sizeof(shm_ringbuf_header_t);

    /* Initialize the header */
    out_ctx->hdr->magic = SHM_RINGBUF_MAGIC;
    out_ctx->hdr->version = SHM_RINGBUF_VERSION;
    out_ctx->hdr->head = 0;
    out_ctx->hdr->tail = 0;
    out_ctx->hdr->bufsize = bufsize;
    out_ctx->hdr->sample_rate = sample_rate;
    out_ctx->hdr->sample_size = sample_size;

    fprintf(stderr, "[SHM] Created ring buffer: %s (size=%zu, sample_rate=%u, sample_size=%u)\n",
            SHM_RINGBUF_NAME, bufsize, sample_rate, sample_size);

    return 0;
}

int shm_ringbuf_open(shm_ringbuf_t *out_ctx)
{
    if (!out_ctx) {
        errno = EINVAL;
        return -1;
    }

    memset(out_ctx, 0, sizeof(*out_ctx));

    /* Open existing shared memory */
    int shm_fd = shm_open(SHM_RINGBUF_NAME, O_RDWR, 0);
    if (shm_fd < 0) {
        perror("shm_open");
        fprintf(stderr, "[SHM] Failed to open: %s\n", SHM_RINGBUF_NAME);
        return -1;
    }

    /* Get the size via fstat */
    struct stat sb;
    if (fstat(shm_fd, &sb) < 0) {
        perror("fstat");
        close(shm_fd);
        return -1;
    }

    size_t total_size = sb.st_size;

    /* Map the shared memory */
    void *region = mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (region == MAP_FAILED) {
        perror("mmap");
        close(shm_fd);
        return -1;
    }

    /* Initialize the context */
    out_ctx->shm_fd = shm_fd;
    out_ctx->region = region;
    out_ctx->region_size = total_size;
    out_ctx->hdr = (shm_ringbuf_header_t *)region;
    out_ctx->buffer = (uint8_t *)region + sizeof(shm_ringbuf_header_t);

    /* Validate the header */
    if (out_ctx->hdr->magic != SHM_RINGBUF_MAGIC) {
        fprintf(stderr, "[SHM] Invalid ring buffer magic (got 0x%x, expected 0x%x)\n",
                out_ctx->hdr->magic, SHM_RINGBUF_MAGIC);
        munmap(region, total_size);
        close(shm_fd);
        return -1;
    }

    fprintf(stderr, "[SHM] Opened ring buffer: %s (size=%zu, sample_rate=%u, sample_size=%u)\n",
            SHM_RINGBUF_NAME, out_ctx->hdr->bufsize, 
            out_ctx->hdr->sample_rate, out_ctx->hdr->sample_size);

    return 0;
}

int shm_ringbuf_close(shm_ringbuf_t *ctx)
{
    if (!ctx || ctx->region == NULL) {
        return 0;
    }

    if (munmap(ctx->region, ctx->region_size) < 0) {
        perror("munmap");
        return -1;
    }

    if (close(ctx->shm_fd) < 0) {
        perror("close");
        return -1;
    }

    memset(ctx, 0, sizeof(*ctx));
    return 0;
}

void shm_ringbuf_destroy(void)
{
    shm_unlink(SHM_RINGBUF_NAME);
    fprintf(stderr, "[SHM] Destroyed ring buffer: %s\n", SHM_RINGBUF_NAME);
}

size_t shm_ringbuf_available(const shm_ringbuf_t *ctx)
{
    if (!ctx || !ctx->hdr) return 0;
    uint64_t head = __atomic_load_n(&ctx->hdr->head, __ATOMIC_ACQUIRE);
    uint64_t tail = __atomic_load_n(&ctx->hdr->tail, __ATOMIC_ACQUIRE);
    return (size_t)(head - tail);
}

size_t shm_ringbuf_free(const shm_ringbuf_t *ctx)
{
    if (!ctx || !ctx->hdr) return 0;
    size_t bufsize = ctx->hdr->bufsize;
    return bufsize - shm_ringbuf_available(ctx);
}

ssize_t shm_ringbuf_write(shm_ringbuf_t *ctx, const void *data, size_t len)
{
    if (!ctx || !ctx->hdr || !data || len == 0) {
        errno = EINVAL;
        return -1;
    }

    size_t bufsize = ctx->hdr->bufsize;
    size_t free_space = shm_ringbuf_free(ctx);

    if (free_space == 0) {
        errno = EAGAIN;
        return -1;  /* Buffer full */
    }

    /* Clamp to available free space */
    size_t to_write = (len <= free_space) ? len : free_space;

    uint64_t head = __atomic_load_n(&ctx->hdr->head, __ATOMIC_ACQUIRE);
    size_t write_pos = head % bufsize;
    size_t space_to_end = bufsize - write_pos;

    if (to_write <= space_to_end) {
        /* Write doesn't wrap around */
        memcpy(&ctx->buffer[write_pos], data, to_write);
    } else {
        /* Write wraps around the ring */
        size_t first_part = space_to_end;
        size_t second_part = to_write - first_part;
        memcpy(&ctx->buffer[write_pos], data, first_part);
        memcpy(&ctx->buffer[0], (uint8_t *)data + first_part, second_part);
    }

    /* Update head pointer */
    __atomic_add_fetch(&ctx->hdr->head, to_write, __ATOMIC_RELEASE);

    return to_write;
}

ssize_t shm_ringbuf_read(shm_ringbuf_t *ctx, void *out_buf, size_t max_len, int timeout_ms)
{
    if (!ctx || !ctx->hdr || !out_buf || max_len == 0) {
        errno = EINVAL;
        return -1;
    }

    size_t bufsize = ctx->hdr->bufsize;
    size_t available = shm_ringbuf_available(ctx);

    if (available == 0) {
        if (timeout_ms == 0) {
            return 0;  /* Non-blocking: return immediately */
        }
        /* TODO: Could add eventfd-based waiting here for blocking reads */
        return 0;
    }

    /* Clamp to max_len */
    size_t to_read = (max_len <= available) ? max_len : available;

    uint64_t tail = __atomic_load_n(&ctx->hdr->tail, __ATOMIC_ACQUIRE);
    size_t read_pos = tail % bufsize;
    size_t space_to_end = bufsize - read_pos;

    if (to_read <= space_to_end) {
        /* Read doesn't wrap around */
        memcpy(out_buf, &ctx->buffer[read_pos], to_read);
    } else {
        /* Read wraps around the ring */
        size_t first_part = space_to_end;
        size_t second_part = to_read - first_part;
        memcpy(out_buf, &ctx->buffer[read_pos], first_part);
        memcpy((uint8_t *)out_buf + first_part, &ctx->buffer[0], second_part);
    }

    /* Update tail pointer */
    __atomic_add_fetch(&ctx->hdr->tail, to_read, __ATOMIC_RELEASE);

    return to_read;
}
