/* qw_ring.h -- lock-free single-writer seqlock ring in shared memory.
 *
 * One writer (acquisition) and any number of readers (detector). No
 * cross-process mutex: a reader never blocks the writer and never returns
 * torn data; it retries a bounded number of times and otherwise reports
 * QW_ST_BUSY or QW_ST_DETACHED so the caller can fail open.
 *
 * Layout: qw_ring_hdr_t (padded to 16 bytes) followed by n_channels *
 * ring_len 32-bit sample slots, channel-major. Sample slots are accessed
 * with relaxed atomics so concurrent reads are well defined.
 * The memory must be 8-byte aligned (mmap/shm_open memory is).
 */
#ifndef QW_RING_H
#define QW_RING_H

#include "qw_ipc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bytes of shared memory needed, or 0 if the arguments are invalid. */
size_t qw_ring_bytes_needed(uint32_t n_channels, uint32_t ring_len);

/* Initialise a ring in mem. Callers must zero fresh shared memory on
 * first creation (POSIX shm_open + ftruncate zeroes new mappings); on
 * re-init of a live or detached ring, seq_lock advances monotonically so
 * concurrent readers never suffer an ABA mismatch. Header fields and
 * sample slots are written with atomic stores; magic and writer_alive
 * are published last (release). names has n_channels entries. */
qw_status_t qw_ring_init(void *mem, size_t mem_bytes, uint32_t fs, uint32_t n_channels,
                         uint32_t ring_len, const char names[][QW_CHANNEL_NAME_LEN]);

/* Mark the ring dead: writer_alive = 0, magic = 0. Readers then get
 * QW_ST_DETACHED. */
void qw_ring_detach(void *mem);

/* Append n_frame_samples samples per channel. channel_major != 0:
 * samples[ch * n + i]; otherwise interleaved samples[i * n_channels + ch].
 * Single writer only. */
qw_status_t qw_ring_write_frame(void *mem, const float *samples, uint32_t n_frame_samples,
                                int channel_major);

/* Copy the newest min(tail_samples, available) samples per channel,
 * oldest first, into out_channel_major (out[ch * n + i]). The caller must
 * size the buffer for QW_MAX_CHANNELS * tail_samples floats unless it knows
 * n_channels. out_names may be NULL. mem_bytes is the size of the mapping the reader
 * holds; it is checked against the header's claimed geometry before any
 * slot is touched (QW_ST_DETACHED if it does not fit). Retries up to max_retries extra times
 * when the writer is mid-update; returns QW_ST_BUSY if it never settles,
 * QW_ST_DETACHED if the ring is dead or corrupt. Never returns torn data. */
qw_status_t qw_ring_read_tail(const void *mem, size_t mem_bytes, uint32_t tail_samples,
                              float *out_channel_major, uint32_t *out_fs, uint32_t *out_n_channels,
                              uint32_t *out_n_samples, char out_names[][QW_CHANNEL_NAME_LEN],
                              uint32_t max_retries);

#ifdef __cplusplus
}
#endif

#endif /* QW_RING_H */
