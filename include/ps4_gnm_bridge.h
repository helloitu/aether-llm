#pragma once

#include <stdint.h>

struct ggml_compute_params;
struct ggml_tensor;

#ifdef __cplusplus
extern "C" {
#endif

struct ps4_gnm_stats {
    int ready;
    int enabled;
    int offload_enabled;
    int selftest_ok;
    int last_error;
    uint64_t candidates;
    uint64_t calls;
    uint64_t hits;
    uint64_t fallbacks;
    uint64_t cache_bytes;
    uint64_t last_us;
    int last_m;
    int last_n;
    int last_k;
    char status[160];
};

void ps4_gnm_init(void);
int  ps4_gnm_selftest(void);
int  ps4_gnm_try_mul_mat(const struct ggml_compute_params * params, struct ggml_tensor * dst);
void ps4_gnm_set_offload_enabled(int enabled);
void ps4_gnm_clear_cache(void);
void ps4_gnm_get_stats(struct ps4_gnm_stats * out);
const char * ps4_gnm_status_line(void);

#ifdef __cplusplus
}
#endif
