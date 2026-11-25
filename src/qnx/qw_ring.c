/* qw_ring.c -- seqlock shared-memory ring (see qw_ring.h). */
#include "qw_ring.h"

#include <stdint.h>
#include <string.h>

#define HDR_BYTES ((sizeof(qw_ring_hdr_t) + 15u) & ~(size_t)15u)

_Static_assert(sizeof(_Atomic uint32_t) == sizeof(uint32_t), "atomic slot size mismatch");
_Static_assert(sizeof(float) == sizeof(uint32_t), "float must be 32-bit");

static _Atomic uint32_t *slots(void *mem) {
    return (_Atomic uint32_t *)((unsigned char *)mem + HDR_BYTES);
}

static const _Atomic uint32_t *slots_const(const void *mem) {
    return (const _Atomic uint32_t *)((const unsigned char *)mem + HDR_BYTES);
}

size_t qw_ring_bytes_needed(uint32_t n_channels, uint32_t ring_len) {
    if (n_channels == 0 || n_channels > QW_MAX_CHANNELS || ring_len == 0) return 0;
    uint64_t n = (uint64_t)n_channels * ring_len;
    if (n > (SIZE_MAX - HDR_BYTES) / sizeof(uint32_t)) return 0;
    return HDR_BYTES + (size_t)n * sizeof(uint32_t);
}

qw_status_t qw_ring_init(void *mem, size_t mem_bytes, uint32_t fs, uint32_t n_channels,
                         uint32_t ring_len, const char names[][QW_CHANNEL_NAME_LEN]) {
    size_t need = qw_ring_bytes_needed(n_channels, ring_len);
    if (mem == NULL || need == 0 || mem_bytes < need || fs == 0) return QW_ST_BAD_ARGS;
    if (((uintptr_t)mem & 7u) != 0) return QW_ST_BAD_ARGS;

    qw_ring_hdr_t *h = (qw_ring_hdr_t *)mem;
    /* Advance seq_lock monotonically even when magic was cleared by detach(),
     * eliminating ABA on a concurrent reader stalled between s1 and s2. */
    uint32_t prev_seq = atomic_load_explicit(&h->seq_lock, memory_order_relaxed);
    uint32_t odd_seq = (prev_seq + 1u) | 1u;
    uint32_t even_seq = odd_seq + 1u;
    if (even_seq == 0u) even_seq = 2u;
    atomic_store_explicit(&h->seq_lock, odd_seq, memory_order_release);
    atomic_store_explicit(&h->magic, 0u, memory_order_release);
    atomic_store_explicit(&h->writer_alive, 0u, memory_order_release);
    atomic_thread_fence(memory_order_release);

    for (uint32_t c = 0; c < QW_MAX_CHANNELS; c++) {
        for (uint32_t j = 0; j < QW_CHANNEL_NAME_LEN; j++) {
            char ch = 0;
            if (names != NULL && c < n_channels && j + 1 < QW_CHANNEL_NAME_LEN) ch = names[c][j];
            atomic_store_explicit(&h->names[c][j], ch, memory_order_relaxed);
        }
    }
    atomic_store_explicit(&h->version, (uint32_t)QW_IPC_VERSION, memory_order_relaxed);
    atomic_store_explicit(&h->fs, fs, memory_order_relaxed);
    atomic_store_explicit(&h->n_channels, n_channels, memory_order_relaxed);
    atomic_store_explicit(&h->ring_len, ring_len, memory_order_relaxed);
    atomic_store_explicit(&h->write_pos, 0u, memory_order_relaxed);
    atomic_store_explicit(&h->total_written, (uint64_t)0, memory_order_relaxed);

    _Atomic uint32_t *data = slots(mem);
    size_t n_slots = (size_t)n_channels * ring_len;
    for (size_t i = 0; i < n_slots; i++) atomic_store_explicit(&data[i], 0u, memory_order_relaxed);

    /* Publish LAST. */
    atomic_thread_fence(memory_order_release);
    atomic_store_explicit(&h->writer_alive, 1u, memory_order_release);
    atomic_store_explicit(&h->magic, (uint32_t)QW_RING_MAGIC, memory_order_release);
    atomic_store_explicit(&h->seq_lock, even_seq, memory_order_release); /* even */
    return QW_ST_OK;
}

void qw_ring_detach(void *mem) {
    if (mem == NULL) return;
    qw_ring_hdr_t *h = (qw_ring_hdr_t *)mem;
    atomic_store_explicit(&h->writer_alive, 0u, memory_order_release);
    atomic_store_explicit(&h->magic, 0u, memory_order_release);
}

static void store_sample(_Atomic uint32_t *slot, float v) {
    uint32_t bits;
    memcpy(&bits, &v, sizeof bits);
    atomic_store_explicit(slot, bits, memory_order_relaxed);
}

static float load_sample(const _Atomic uint32_t *slot) {
    uint32_t bits = atomic_load_explicit(slot, memory_order_relaxed);
    float v;
    memcpy(&v, &bits, sizeof v);
    return v;
}

qw_status_t qw_ring_write_frame(void *mem, const float *samples, uint32_t n_frame_samples,
                                int channel_major) {
    if (mem == NULL || samples == NULL) return QW_ST_BAD_ARGS;
    qw_ring_hdr_t *h = (qw_ring_hdr_t *)mem;
    if (atomic_load_explicit(&h->magic, memory_order_acquire) != QW_RING_MAGIC ||
        atomic_load_explicit(&h->writer_alive, memory_order_acquire) != 1u ||
        atomic_load_explicit(&h->version, memory_order_relaxed) != (uint32_t)QW_IPC_VERSION) {
        return QW_ST_DETACHED;
    }
    uint32_t nch = atomic_load_explicit(&h->n_channels, memory_order_relaxed);
    uint32_t len = atomic_load_explicit(&h->ring_len, memory_order_relaxed);
    if (nch == 0 || nch > QW_MAX_CHANNELS || len == 0) return QW_ST_DETACHED;
    if (n_frame_samples == 0 || n_frame_samples > len) return QW_ST_BAD_ARGS;

    _Atomic uint32_t *data = slots(mem);
    uint32_t seq = atomic_load_explicit(&h->seq_lock, memory_order_relaxed);
    atomic_store_explicit(&h->seq_lock, seq + 1u, memory_order_relaxed); /* odd */
    atomic_thread_fence(memory_order_release); /* odd store before data stores */

    uint32_t pos = atomic_load_explicit(&h->write_pos, memory_order_relaxed);
    for (uint32_t i = 0; i < n_frame_samples; i++) {
        uint32_t slot = (uint32_t)(((uint64_t)pos + i) % len);
        for (uint32_t c = 0; c < nch; c++) {
            float v = channel_major ? samples[(size_t)c * n_frame_samples + i]
                                    : samples[(size_t)i * nch + c];
            store_sample(&data[(size_t)c * len + slot], v);
        }
    }
    atomic_store_explicit(&h->write_pos, (uint32_t)(((uint64_t)pos + n_frame_samples) % len),
                          memory_order_relaxed);
    uint64_t total = atomic_load_explicit(&h->total_written, memory_order_relaxed);
    atomic_store_explicit(&h->total_written, total + n_frame_samples, memory_order_relaxed);

    atomic_store_explicit(&h->seq_lock, seq + 2u, memory_order_release); /* even */
    return QW_ST_OK;
}

/* Validate the header against the caller's mapping size. Returns QW_ST_OK
 * and the geometry, or QW_ST_DETACHED. Never reads names or sample slots. */
static qw_status_t check_header(const qw_ring_hdr_t *h, size_t mem_bytes, uint32_t *nch,
                                uint32_t *len) {
    if (atomic_load_explicit(&h->magic, memory_order_acquire) != QW_RING_MAGIC ||
        atomic_load_explicit(&h->version, memory_order_relaxed) != (uint32_t)QW_IPC_VERSION) {
        return QW_ST_DETACHED;
    }
    uint32_t c = atomic_load_explicit(&h->n_channels, memory_order_relaxed);
    uint32_t l = atomic_load_explicit(&h->ring_len, memory_order_relaxed);
    size_t need = qw_ring_bytes_needed(c, l);
    if (need == 0 || mem_bytes < need) return QW_ST_DETACHED;
    *nch = c;
    *len = l;
    return QW_ST_OK;
}

qw_status_t qw_ring_read_tail(const void *mem, size_t mem_bytes, uint32_t tail_samples,
                              float *out_channel_major, uint32_t *out_fs, uint32_t *out_n_channels,
                              uint32_t *out_n_samples, char out_names[][QW_CHANNEL_NAME_LEN],
                              uint32_t max_retries) {
    if (mem == NULL) return QW_ST_BAD_ARGS;
    if (((uintptr_t)mem & 7u) != 0) return QW_ST_BAD_ARGS;
    if (mem_bytes < sizeof(qw_ring_hdr_t)) return QW_ST_DETACHED; /* before touching hdr */
    if (out_channel_major == NULL || tail_samples == 0) return QW_ST_BAD_ARGS;
    const qw_ring_hdr_t *h = (const qw_ring_hdr_t *)mem;
    const _Atomic uint32_t *data = slots_const(mem);

    uint32_t nch, len;
    if (check_header(h, mem_bytes, &nch, &len) != QW_ST_OK) return QW_ST_DETACHED;

    for (uint32_t attempt = 0;; attempt++) {
        /* Dead or corrupt ring wins over "busy": a writer that died mid-update
         * leaves seq_lock odd forever. */
        if (atomic_load_explicit(&h->writer_alive, memory_order_acquire) != 1u) {
            return QW_ST_DETACHED;
        }
        uint32_t s1 = atomic_load_explicit(&h->seq_lock, memory_order_acquire);
        if (s1 & 1u) {
            if (attempt >= max_retries) break;
            continue;
        }
        uint32_t c2, l2;
        if (check_header(h, mem_bytes, &c2, &l2) != QW_ST_OK) return QW_ST_DETACHED;
        nch = c2;
        len = l2;
        uint32_t fs = atomic_load_explicit(&h->fs, memory_order_relaxed);
        uint32_t pos = atomic_load_explicit(&h->write_pos, memory_order_relaxed);
        uint64_t total = atomic_load_explicit(&h->total_written, memory_order_relaxed);

        uint64_t avail = total < len ? total : len;
        uint32_t n = (uint64_t)tail_samples < avail ? tail_samples : (uint32_t)avail;
        /* Indices are reduced mod len so a torn header read (discarded by the
         * seq check below) can never index outside the ring. */
        uint32_t start = (uint32_t)(((uint64_t)(pos % len) + len - (n % len)) % len);
        for (uint32_t c = 0; c < nch; c++) {
            for (uint32_t i = 0; i < n; i++) {
                uint32_t slot = (uint32_t)(((uint64_t)start + i) % len);
                out_channel_major[(size_t)c * n + i] = load_sample(&data[(size_t)c * len + slot]);
            }
        }
        char names_copy[QW_MAX_CHANNELS][QW_CHANNEL_NAME_LEN];
        for (uint32_t c = 0; c < QW_MAX_CHANNELS; c++) {
            for (uint32_t j = 0; j < QW_CHANNEL_NAME_LEN; j++) {
                names_copy[c][j] = atomic_load_explicit(&h->names[c][j], memory_order_relaxed);
            }
            names_copy[c][QW_CHANNEL_NAME_LEN - 1] = '\0';
        }

        atomic_thread_fence(memory_order_acquire);
        uint32_t s2 = atomic_load_explicit(&h->seq_lock, memory_order_acquire);
        if (s2 != s1) {
            if (attempt >= max_retries) break;
            continue;
        }
        if (atomic_load_explicit(&h->writer_alive, memory_order_acquire) != 1u ||
            check_header(h, mem_bytes, &c2, &l2) != QW_ST_OK || c2 != nch || l2 != len) {
            return QW_ST_DETACHED;
        }
        if (out_fs) *out_fs = fs;
        if (out_n_channels) *out_n_channels = nch;
        if (out_n_samples) *out_n_samples = n;
        if (out_names) {
            for (uint32_t c = 0; c < nch; c++) memcpy(out_names[c], names_copy[c], QW_CHANNEL_NAME_LEN);
        }
        return QW_ST_OK;
    }
    return QW_ST_BUSY;
}
