
#include "ps4_gnm_bridge.h"

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "ggml.h"
#define GGML_COMMON_DECL_CPP
#include "ggml-common.h"
#include "ggml-cpu-impl.h"
#include "ggml-quants.h"

extern "C" int      sceKernelDebugOutText(int ch, const char *text, ...);
extern "C" int      sceKernelAllocateDirectMemory(int64_t, int64_t, size_t, size_t, int, int64_t *);
extern "C" int      sceKernelMapDirectMemory(void **, size_t, int, int, int64_t, size_t);
extern "C" int      sceKernelMunmap(void *, size_t);
extern "C" int32_t  sceKernelReleaseDirectMemory(int64_t, size_t);
extern "C" size_t   sceKernelGetDirectMemorySize(void);
extern "C" int      sceKernelAvailableDirectMemorySize(int64_t, int64_t, size_t, int64_t *, size_t *);
extern "C" int      sceKernelUsleep(unsigned int);
extern "C" uint64_t sceKernelGetProcessTime(void);
extern "C" uint32_t sceKernelLoadStartModule(const char *, size_t, const void *, uint32_t, void *, void *);
extern "C" int      sceKernelDlsym(uint32_t, const char *, void **);

typedef int (*SubmitCommandBuffers_fn)(uint32_t, const uint32_t **, uint32_t *, const uint32_t **, uint32_t *);
typedef int (*SubmitDone_fn)(void);

static const int    WB_ONION   = 0;
static const int    WC_GARLIC  = 3;
static const int    PROT_RW    = 0x33;
static const size_t DMEM_ALIGN = 0x4000;
static const uint64_t CACHE_LIMIT_BYTES = 1536ull << 20;

static const uint32_t SHADER_COMPUTE      = 1;
static const uint32_t IT_DISPATCH_DIRECT  = 0x15;
static const uint32_t IT_SET_SH_REG       = 0x76;
static const uint32_t R_NUM_THREAD_X      = 0x207;
static const uint32_t R_PGM_LO            = 0x20C;
static const uint32_t R_PGM_RSRC1         = 0x212;
static const uint32_t R_USER_DATA_0       = 0x240;

#include "gnm_shaders.h"

struct DmemBuf {
    uint8_t *ptr;
    uint64_t gpu;
    int64_t phys;
    size_t size;
};

struct CacheEntry {
    const void *src;
    int type;
    int m;
    int k;
    size_t nb01;
    DmemBuf wt;
};

static SubmitCommandBuffers_fn g_submit = NULL;
static SubmitDone_fn g_done = NULL;
static DmemBuf g_ctrl = {};
static uint8_t *g_shader_ptr = NULL;
static uint32_t *g_ring = NULL;
static uint64_t g_shader_gpu = 0;
static DmemBuf g_scratch = {};
static CacheEntry g_cache[256];
static int g_cache_count = 0;
static int g_ready = 0;
static int g_enabled = 1;
static int g_offload_enabled = 0;
static int g_selftest_ok = 0;
static int g_last_error = 0;
static int g_op_ok = 0;
static uint64_t g_candidates = 0;
static uint64_t g_calls = 0;
static uint64_t g_hits = 0;
static uint64_t g_fallbacks = 0;
static uint64_t g_cache_bytes = 0;
static uint64_t g_last_us = 0;
static int g_last_m = 0;
static int g_last_n = 0;
static int g_last_k = 0;
static char g_status[160] = "GNM not initialized";
static int g_verify_budget = 64;
static int g_candidate_log_budget = 64;

static void glog(const char *fmt, ...) {
    char msg[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    sceKernelDebugOutText(0, "LLM-GNM ");
    sceKernelDebugOutText(0, msg);
    sceKernelDebugOutText(0, "\n");
    snprintf(g_status, sizeof(g_status), "%s", msg);
}

static size_t align_up(size_t v, size_t a) {
    return (v + a - 1) & ~(a - 1);
}

static void *dmem_alloc(DmemBuf *out, size_t len, int mem_type, const char *tag) {
    len = align_up(len, DMEM_ALIGN);
    int64_t phys = 0;
    int rc = sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(), len, DMEM_ALIGN, mem_type, &phys);
    if (rc != 0) {
        g_last_error = rc;
        glog("alloc %s failed 0x%08x (%u MB)", tag, rc, (unsigned)(len >> 20));
        return NULL;
    }

    void *va = NULL;
    rc = sceKernelMapDirectMemory(&va, len, PROT_RW, 0, phys, DMEM_ALIGN);
    if (rc != 0) {
        g_last_error = rc;
        glog("map %s failed 0x%08x", tag, rc);
        sceKernelReleaseDirectMemory(phys, len);
        return NULL;
    }

    out->ptr = (uint8_t *)va;
    out->gpu = (uint64_t)va;
    out->phys = phys;
    out->size = len;
    return va;
}

static void dmem_free(DmemBuf *buf) {
    if (!buf || !buf->ptr) return;
    sceKernelMunmap(buf->ptr, buf->size);
    sceKernelReleaseDirectMemory(buf->phys, buf->size);
    memset(buf, 0, sizeof(*buf));
}

static uint32_t pm4_type3(uint32_t op, uint32_t body_dw, uint32_t shader_type) {
    return (3u << 30) | ((body_dw - 1) << 16) | (op << 8) | (shader_type << 1);
}

static uint32_t *set_sh_reg(uint32_t *cb, uint32_t reg, const uint32_t *vals, uint32_t n) {
    *cb++ = pm4_type3(IT_SET_SH_REG, n + 1, SHADER_COMPUTE);
    *cb++ = reg;
    for (uint32_t i = 0; i < n; i++) *cb++ = vals[i];
    return cb;
}

static uint32_t *write_dispatch_init(uint32_t *cb) {
    uint32_t v;
    v = 0xFFFFFFFFu; cb = set_sh_reg(cb, 0x216, &v, 1);
    v = 0xFFFFFFFFu; cb = set_sh_reg(cb, 0x217, &v, 1);
    v = 0x170u;      cb = set_sh_reg(cb, 0x215, &v, 1);
    *cb++ = pm4_type3(0x58, 6, 0);
    *cb++ = 0x28000000u; *cb++ = 0; *cb++ = 0; *cb++ = 0; *cb++ = 0; *cb++ = 0xAu;
    return cb;
}

static void make_vsharp(uint32_t o[4], uint64_t addr, uint32_t num_bytes) {
    o[0] = (uint32_t)(addr & 0xFFFFFFFFu);
    o[1] = (uint32_t)((addr >> 32) & 0xFFFFu);
    o[2] = num_bytes;
    o[3] = 0x00024FACu;
}

void ps4_gnm_init(void) {
    if (g_ready || !g_enabled) return;

    uint32_t h = sceKernelLoadStartModule("/system/common/lib/libSceGnmDriver.sprx", 0, NULL, 0, NULL, NULL);
    void *p_submit = NULL;
    void *p_done = NULL;
    int e1 = sceKernelDlsym(h, "sceGnmSubmitCommandBuffers", &p_submit);
    int e2 = sceKernelDlsym(h, "sceGnmSubmitDone", &p_done);
    if (!p_submit || !p_done) {
        g_last_error = e1 ? e1 : e2;
        glog("GNM dlsym failed h=0x%08x submit=%d done=%d", h, e1, e2);
        return;
    }

    g_submit = (SubmitCommandBuffers_fn)p_submit;
    g_done = (SubmitDone_fn)p_done;
    if (!dmem_alloc(&g_ctrl, 2u << 20, WB_ONION, "ctrl")) return;

    g_shader_ptr = g_ctrl.ptr;
    g_shader_gpu = g_ctrl.gpu;
    memcpy(g_shader_ptr, g_matvec_t_shader, sizeof(g_matvec_t_shader));
    g_ring = (uint32_t *)(g_ctrl.ptr + 4096);
    if (((g_shader_gpu >> 40) & 0xFFu) != 0) {
        g_last_error = -1;
        glog("shader address too high");
        return;
    }

    g_ready = 1;
    glog("GNM ready");
}

static int ensure_scratch(int k, int m) {
    size_t need = align_up((size_t)k * sizeof(float), 256) + align_up((size_t)m * sizeof(float), 256);
    if (g_scratch.ptr && g_scratch.size >= need) return 1;
    return dmem_alloc(&g_scratch, need, WB_ONION, "scratch") != NULL;
}

static void unpack_q4_scales(const block_q4_K *x, uint8_t scales[8], uint8_t mins[8]) {
    uint32_t utmp[4] = {};
    memcpy(utmp, x->scales, 12);
    static const uint32_t kmask1 = 0x3f3f3f3f;
    static const uint32_t kmask2 = 0x0f0f0f0f;
    static const uint32_t kmask3 = 0x03030303;
    utmp[3] = ((utmp[2] >> 4) & kmask2) | (((utmp[1] >> 6) & kmask3) << 4);
    const uint32_t uaux = utmp[1] & kmask1;
    utmp[1] = (utmp[2] & kmask2) | (((utmp[0] >> 6) & kmask3) << 4);
    utmp[2] = uaux;
    utmp[0] &= kmask1;
    memcpy(scales, &utmp[0], 8);
    memcpy(mins, &utmp[2], 8);
}

static void dequant_q4_row_to_wt(const block_q4_K *row, int k, int m, int r, float *wt) {
    const int nb = k / QK_K;
    for (int ib = 0; ib < nb; ib++) {
        const block_q4_K *b = row + ib;
        uint8_t scales[8], mins[8];
        unpack_q4_scales(b, scales, mins);
        const float d = ggml_fp16_to_fp32((ggml_fp16_t)b->data.data.d);
        const float dmin = ggml_fp16_to_fp32((ggml_fp16_t)b->data.data.dmin);
        for (int j = 0; j < QK_K; j++) {
            const uint8_t q = (j & 32) ? (b->qs[(j & 31) + (j / 64) * 32] >> 4)
                                       : (b->qs[(j & 31) + (j / 64) * 32] & 0x0F);
            const int g = j / 32;
            wt[((ib * QK_K + j) * m) + r] = d * scales[g] * (float)q - dmin * mins[g];
        }
    }
}

static void dequant_q6_row_to_wt(const block_q6_K *row, int k, int m, int r, float *wt) {
    const int nb = k / QK_K;
    for (int ib = 0; ib < nb; ib++) {
        const block_q6_K *b = row + ib;
        const float d = ggml_fp16_to_fp32((ggml_fp16_t)b->d);
        const uint8_t *ql = b->ql;
        const uint8_t *qh = b->qh;
        const int8_t *sc = b->scales;
        for (int n = 0; n < QK_K; n += 128) {
            for (int l = 0; l < 32; l++) {
                int is = l / 16;
                const int q1 = (int)((ql[l +  0] & 0xF) | (((qh[l] >> 0) & 3) << 4)) - 32;
                const int q2 = (int)((ql[l + 32] & 0xF) | (((qh[l] >> 2) & 3) << 4)) - 32;
                const int q3 = (int)((ql[l +  0] >>  4) | (((qh[l] >> 4) & 3) << 4)) - 32;
                const int q4 = (int)((ql[l + 32] >>  4) | (((qh[l] >> 6) & 3) << 4)) - 32;
                const int base = ib * QK_K + n;
                wt[(base + l +  0) * m + r] = d * sc[is + 0] * q1;
                wt[(base + l + 32) * m + r] = d * sc[is + 2] * q2;
                wt[(base + l + 64) * m + r] = d * sc[is + 4] * q3;
                wt[(base + l + 96) * m + r] = d * sc[is + 6] * q4;
            }
            ql += 64;
            qh += 32;
            sc += 8;
        }
    }
}

static void dequant_q5_row_to_wt(const block_q5_K *row, int k, int m, int r, float *wt, float *tmp) {
    dequantize_row_q5_K(row, tmp, k);
    for (int c = 0; c < k; c++) wt[(size_t)c * m + r] = tmp[c];
}

static CacheEntry *find_or_make_cache(const ggml_tensor *src0, const char *base) {
    const int k = (int)src0->ne[0];
    const int m = (int)src0->ne[1];
    for (int i = 0; i < g_cache_count; i++) {
        CacheEntry *e = &g_cache[i];
        if (e->src == base && e->type == src0->type && e->m == m && e->k == k && e->nb01 == src0->nb[1]) {
            return e;
        }
    }

    if (g_cache_count >= (int)(sizeof(g_cache) / sizeof(g_cache[0]))) {
        g_last_error = -2;
        glog("cache table full");
        return NULL;
    }

    CacheEntry *e = &g_cache[g_cache_count];
    memset(e, 0, sizeof(*e));
    e->src = base;
    e->type = src0->type;
    e->m = m;
    e->k = k;
    e->nb01 = src0->nb[1];

    const size_t bytes = (size_t)m * (size_t)k * sizeof(float);
    if (g_cache_bytes + bytes > CACHE_LIMIT_BYTES) {
        g_last_error = -3;
        glog("cache budget full %.1f/%.1f MB",
             (double)g_cache_bytes / (1024.0 * 1024.0),
             (double)CACHE_LIMIT_BYTES / (1024.0 * 1024.0));
        memset(e, 0, sizeof(*e));
        return NULL;
    }
    if (!dmem_alloc(&e->wt, bytes, WC_GARLIC, "W_T")) {
        memset(e, 0, sizeof(*e));
        return NULL;
    }

    float *wt = (float *)malloc(bytes);
    if (!wt) {
        g_last_error = -4;
        glog("cache temp malloc failed %.1f MB", (double)bytes / (1024.0 * 1024.0));
        dmem_free(&e->wt);
        memset(e, 0, sizeof(*e));
        return NULL;
    }
    float *tmp_q5 = NULL;
    if (src0->type == GGML_TYPE_Q5_K) {
        tmp_q5 = (float *)malloc((size_t)k * sizeof(float));
        if (!tmp_q5) {
            g_last_error = -4;
            glog("cache q5 temp malloc failed %.1f MB", (double)k * sizeof(float) / (1024.0 * 1024.0));
            free(wt);
            dmem_free(&e->wt);
            memset(e, 0, sizeof(*e));
            return NULL;
        }
    }
    const uint64_t t0 = sceKernelGetProcessTime();
    for (int r = 0; r < m; r++) {
        const char *row = base + (size_t)r * src0->nb[1];
        if (src0->type == GGML_TYPE_F32) {
            const float *f = (const float *)row;
            for (int c = 0; c < k; c++) wt[(size_t)c * m + r] = f[c];
        } else if (src0->type == GGML_TYPE_Q4_K) {
            dequant_q4_row_to_wt((const block_q4_K *)row, k, m, r, wt);
        } else if (src0->type == GGML_TYPE_Q5_K) {
            dequant_q5_row_to_wt((const block_q5_K *)row, k, m, r, wt, tmp_q5);
        } else if (src0->type == GGML_TYPE_Q6_K) {
            dequant_q6_row_to_wt((const block_q6_K *)row, k, m, r, wt);
        }
    }
    memcpy(e->wt.ptr, wt, bytes);
    __builtin_ia32_sfence();
    free(tmp_q5);
    free(wt);
    g_cache_count++;
    g_cache_bytes += bytes;
    glog("cached %s M=%d K=%d %.1f MB in %llu us",
         ggml_type_name(src0->type), m, k, (double)bytes / (1024.0 * 1024.0),
         (unsigned long long)(sceKernelGetProcessTime() - t0));
    return e;
}

static int run_matvec(CacheEntry *ce, const float *x, float *y) {
    const int k = ce->k;
    const int m = ce->m;
    if (!ensure_scratch(k, m)) return 0;

    uint8_t *p = g_scratch.ptr;
    float *gx = (float *)p;
    uint64_t gx_gpu = g_scratch.gpu;
    p += align_up((size_t)k * sizeof(float), 256);
    float *gy = (float *)p;
    uint64_t gy_gpu = g_scratch.gpu + (uint64_t)(p - g_scratch.ptr);

    memcpy(gx, x, (size_t)k * sizeof(float));
    memset(gy, 0, (size_t)m * sizeof(float));

    uint32_t ud[14];
    make_vsharp(&ud[0], ce->wt.gpu, (uint32_t)((uint64_t)m * k * sizeof(float)));
    make_vsharp(&ud[4], gx_gpu, (uint32_t)(k * sizeof(float)));
    make_vsharp(&ud[8], gy_gpu, (uint32_t)(m * sizeof(float)));
    ud[12] = (uint32_t)k;
    ud[13] = (uint32_t)m;

    uint32_t *cb = g_ring;
    cb = write_dispatch_init(cb);
    cb = set_sh_reg(cb, R_USER_DATA_0, ud, 14);
    uint32_t pgm[2] = { (uint32_t)((g_shader_gpu >> 8) & 0xFFFFFFFFu), 0 };
    uint32_t rsrc[2] = { 2u | (2u << 6), (14u << 1) | (1u << 7) };
    uint32_t nthr[3] = { 64, 1, 1 };
    cb = set_sh_reg(cb, R_PGM_LO, pgm, 2);
    cb = set_sh_reg(cb, R_PGM_RSRC1, rsrc, 2);
    cb = set_sh_reg(cb, R_NUM_THREAD_X, nthr, 3);
    *cb++ = pm4_type3(IT_DISPATCH_DIRECT, 4, SHADER_COMPUTE);
    *cb++ = (uint32_t)(m / 64); *cb++ = 1; *cb++ = 1; *cb++ = 1;

    const uint32_t *dcb[1] = { g_ring };
    uint32_t sz[1] = { (uint32_t)((cb - g_ring) * sizeof(uint32_t)) };
    __builtin_ia32_sfence();
    int rc = g_submit(1, dcb, sz, NULL, NULL);
    if (rc != 0) {
        g_last_error = rc;
        glog("submit failed 0x%08x", rc);
        return 0;
    }
    rc = g_done();
    if (rc != 0) {
        g_last_error = rc;
        glog("submit done failed 0x%08x", rc);
        return 0;
    }

    memcpy(y, gy, (size_t)m * sizeof(float));
    return 1;
}

static int verify_matvec(const CacheEntry *ce, const float *x, const float *y) {
    if (g_verify_budget <= 0) return 1;
    g_verify_budget--;
    const int k = ce->k, m = ce->m;
    const float *wt = (const float *)ce->wt.ptr;
    int bad = 0;
    float maxerr = 0.0f, maxrel = 0.0f;
    for (int r = 0; r < m; r++) {
        float ref = 0.0f;
        for (int c = 0; c < k; c++) ref += wt[(size_t)c * m + r] * x[c];
        float d = y[r] - ref; if (d < 0.0f) d = -d;
        float denom = (ref < 0.0f ? -ref : ref) + 1.0f;
        if (d > maxerr) maxerr = d;
        if (d / denom > maxrel) maxrel = d / denom;
        if (d > 1e-2f * denom) bad++;
    }
    glog("verify %s M=%d K=%d bad=%d/%d maxerr=%.4f maxrel=%.4f",
         bad == 0 ? "OK" : "MISMATCH", m, k, bad, m, maxerr, maxrel);
    if (bad != 0) g_enabled = 0;
    return bad == 0;
}

static int supported_mul_mat(const ggml_tensor *dst) {
    const ggml_tensor *src0 = dst->src[0];
    const ggml_tensor *src1 = dst->src[1];
    if (!g_enabled || !src0 || !src1) return 0;
    if (dst->type != GGML_TYPE_F32 || src1->type != GGML_TYPE_F32) return 0;
    if (src0->type != GGML_TYPE_F32 && src0->type != GGML_TYPE_Q4_K && src0->type != GGML_TYPE_Q5_K && src0->type != GGML_TYPE_Q6_K) return 0;
    if (src0->ne[0] != src1->ne[0] || dst->ne[0] != src0->ne[1] || dst->ne[1] != src1->ne[1]) return 0;
    if ((src0->ne[1] % 64) != 0 || src0->ne[0] <= 0 || src0->ne[1] <= 0) return 0;
    const bool proven_big = src0->ne[1] >= 2048 && src0->ne[0] >= 4096;
    const bool qwen_down  = src0->ne[1] == 896  && src0->ne[0] == 4864;
    if (!proven_big && !qwen_down) return 0;
    if ((src0->type == GGML_TYPE_Q4_K || src0->type == GGML_TYPE_Q5_K || src0->type == GGML_TYPE_Q6_K) && (src0->ne[0] % QK_K) != 0) return 0;
    if (src0->ne[1] > 262144 || src0->ne[0] > 32768) return 0;
    if (src0->nb[0] != ggml_type_size(src0->type) || src1->nb[0] != sizeof(float) || dst->nb[0] != sizeof(float)) return 0;
    return 1;
}

static void log_candidate(const ggml_tensor *dst) {
    if (g_candidate_log_budget <= 0) return;
    g_candidate_log_budget--;
    const ggml_tensor *src0 = dst->src[0];
    const ggml_tensor *src1 = dst->src[1];
    const int n = (int)(src1->ne[1] * src1->ne[2] * src1->ne[3]);
    glog("candidate %s M=%d K=%d N=%d nb0=%u nb1=%u offload=%d",
         ggml_type_name(src0->type), (int)src0->ne[1], (int)src0->ne[0], n,
         (unsigned)src0->nb[0], (unsigned)src0->nb[1], g_offload_enabled);
}

static int do_mul_mat(const ggml_tensor *dst) {
    ps4_gnm_init();
    if (!g_ready || !g_selftest_ok) return 0;

    const ggml_tensor *src0 = dst->src[0];
    const ggml_tensor *src1 = dst->src[1];
    const int64_t r2 = src1->ne[2] / src0->ne[2];
    const int64_t r3 = src1->ne[3] / src0->ne[3];
    if (src0->ne[2] <= 0 || src0->ne[3] <= 0 || r2 <= 0 || r3 <= 0) return 0;

    const uint64_t t0 = sceKernelGetProcessTime();
    const int m = (int)src0->ne[1];
    const int k = (int)src0->ne[0];
    const int n = (int)(src1->ne[1] * src1->ne[2] * src1->ne[3]);

    for (int64_t i13 = 0; i13 < src1->ne[3]; i13++) {
        for (int64_t i12 = 0; i12 < src1->ne[2]; i12++) {
            const int64_t i03 = i13 / r3;
            const int64_t i02 = i12 / r2;
            const char *wbase = (const char *)src0->data + i02 * src0->nb[2] + i03 * src0->nb[3];
            CacheEntry *ce = find_or_make_cache(src0, wbase);
            if (!ce) return 0;
            for (int64_t i11 = 0; i11 < src1->ne[1]; i11++) {
                const float *x = (const float *)((const char *)src1->data + i11 * src1->nb[1] + i12 * src1->nb[2] + i13 * src1->nb[3]);
                float *y = (float *)((char *)dst->data + i11 * dst->nb[1] + i12 * dst->nb[2] + i13 * dst->nb[3]);
                if (!run_matvec(ce, x, y)) return 0;
                if (!verify_matvec(ce, x, y)) return 0;
            }
        }
    }

    g_last_us = sceKernelGetProcessTime() - t0;
    g_last_m = m;
    g_last_n = n;
    g_last_k = k;
    g_hits++;
    return 1;
}

int ps4_gnm_try_mul_mat(const struct ggml_compute_params *params, struct ggml_tensor *dst) {
    if (!supported_mul_mat(dst)) return 0;
    if (params->ith == 0) {
        g_candidates++;
        log_candidate(dst);
    }
    if (!g_offload_enabled) return 0;
    g_calls++;

    if (params->ith == 0) {
        g_op_ok = do_mul_mat(dst);
        if (!g_op_ok) g_fallbacks++;
    }
    ggml_barrier(params->threadpool);
    return g_op_ok;
}

void ps4_gnm_set_offload_enabled(int enabled) {
    g_offload_enabled = enabled ? 1 : 0;
    glog("offload %s", g_offload_enabled ? "ENABLED" : "disabled");
}

static int selftest_shape(int m, int k) {
    CacheEntry ce = {};
    ce.type = GGML_TYPE_F32; ce.m = m; ce.k = k;
    const size_t wbytes = (size_t)m * (size_t)k * sizeof(float);
    if (!dmem_alloc(&ce.wt, wbytes, WC_GARLIC, "st_W")) {
        glog("selftest M=%d K=%d SKIP: garlic alloc 0x%08x (%u MB)", m, k, g_last_error,
             (unsigned)(wbytes >> 20));
        return -1;
    }
    float *x = (float *)malloc((size_t)k * sizeof(float));
    float *y = (float *)malloc((size_t)m * sizeof(float));
    if (!x || !y) { free(x); free(y); dmem_free(&ce.wt); glog("selftest M=%d K=%d SKIP heap", m, k); return -1; }
    for (int c = 0; c < k; c++) x[c] = 1.0f;
    float *wt = (float *)ce.wt.ptr;

    for (size_t i = 0; i < (size_t)m * (size_t)k; i++) wt[i] = 1.0f;
    __builtin_ia32_sfence();
    int ranA = 0; for (int i = 0; i < 2; i++) ranA = run_matvec(&ce, x, y);
    int badA = 0, firstA = -1;
    for (int r = 0; r < m; r++) { float d = y[r] - (float)k; if (d < 0.0f) d = -d;
        if (d > 0.5f) { if (firstA < 0) firstA = r; badA++; } }
    const float yA0 = y[0];
    const float yAfirst = (firstA >= 0) ? y[firstA] : 0.0f;

    for (int c = 0; c < k; c++) for (int r = 0; r < m; r++) wt[(size_t)c * m + r] = (float)r;
    __builtin_ia32_sfence();
    int ranB = 0; for (int i = 0; i < 2; i++) ranB = run_matvec(&ce, x, y);
    int badB = 0, firstB = -1;
    for (int r = 0; r < m; r++) { float d = y[r] - (float)r * (float)k; if (d < 0.0f) d = -d;
        if (d > 0.5f) { if (firstB < 0) firstB = r; badB++; } }

    glog("selftest M=%d K=%d | A(ones) bad=%d/%d y0=%.0f exp=%d | B(row) bad=%d/%d yLast=%.0f exp=%d",
         m, k, badA, m, yA0, k, badB, m, y[m - 1], (m - 1) * k);
    if (badA) glog("  A first-bad row=%d got=%.1f exp=%d (ran=%d)", firstA, yAfirst, k, ranA);
    if (badB) glog("  B first-bad row=%d got=%.1f exp=%d (ran=%d)", firstB, y[firstB], firstB * k, ranB);

    int badC = 0, firstC = -1, badG = 0, ranC = 0;
    float *wt_cpu = (float *)malloc(wbytes);
    if (wt_cpu) {
        for (int c = 0; c < k; c++) x[c] = (float)((c % 13) - 6) * 0.5f;
        for (int c = 0; c < k; c++)
            for (int r = 0; r < m; r++)
                wt_cpu[(size_t)c * m + r] = (float)(((c + 3 * r) % 17) - 8) * 0.25f;
        memcpy(ce.wt.ptr, wt_cpu, wbytes);
        __builtin_ia32_sfence();
        for (int i = 0; i < 2; i++) ranC = run_matvec(&ce, x, y);
        for (int r = 0; r < m; r++) {
            float s = 0.0f;
            for (int c = 0; c < k; c++) s += wt_cpu[(size_t)c * m + r] * x[c];
            float d = y[r] - s; if (d < 0.0f) d = -d;
            float den = (s < 0.0f ? -s : s) + 1.0f;
            if (d > 1e-2f * den) { if (firstC < 0) { firstC = r; }
                if (badC == 0) glog("  C first-bad row=%d got=%.4f ref=%.4f", r, y[r], s);
                badC++; }
        }
        const float *wg = (const float *)ce.wt.ptr;
        size_t step = (size_t)m * k / 4096; if (step == 0) step = 1;
        for (size_t i = 0; i < (size_t)m * k; i += step) {
            float d = wg[i] - wt_cpu[i]; if (d < 0.0f) d = -d;
            if (d > 1e-3f) badG++;
        }
        free(wt_cpu);
    }
    glog("selftest M=%d K=%d | C(varied x,W) bad=%d/%d ran=%d | garlic-readback bad=%d/4096",
         m, k, badC, m, ranC, badG);

    free(x); free(y); dmem_free(&ce.wt);
    return (ranA && ranB && badA == 0 && badB == 0 && badC == 0) ? 1 : 0;
}

int ps4_gnm_selftest(void) {
    ps4_gnm_init();
    g_selftest_ok = 0;
    if (!g_ready) return 0;

    int64_t availPhys = 0; size_t availSz = 0;
    int arc = sceKernelAvailableDirectMemorySize(0, sceKernelGetDirectMemorySize(),
                                                 DMEM_ALIGN, &availPhys, &availSz);
    glog("selftest start: garlic avail=%u MB", (arc == 0) ? (unsigned)(availSz >> 20) : 0);

    static const struct { int m, k; } shapes[] = {
        { 128,  256 },
        { 896,  256 },
        { 128, 4864 },
        { 896, 4864 },
        { 2048, 4096 },
    };
    int baseline_ok = 0;
    for (unsigned i = 0; i < sizeof(shapes) / sizeof(shapes[0]); i++) {
        int r = selftest_shape(shapes[i].m, shapes[i].k);
        if (i == 0) baseline_ok = (r == 1);
    }

    g_selftest_ok = baseline_ok;
    glog("selftest done: baseline %s (see per-shape lines above)", baseline_ok ? "PASS" : "FAIL");
    return g_selftest_ok;
}

void ps4_gnm_clear_cache(void) {
    for (int i = 0; i < g_cache_count; i++) {
        dmem_free(&g_cache[i].wt);
    }
    memset(g_cache, 0, sizeof(g_cache));
    g_cache_count = 0;
    g_cache_bytes = 0;
}

void ps4_gnm_get_stats(struct ps4_gnm_stats *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->ready = g_ready;
    out->enabled = g_enabled;
    out->offload_enabled = g_offload_enabled;
    out->selftest_ok = g_selftest_ok;
    out->last_error = g_last_error;
    out->candidates = g_candidates;
    out->calls = g_calls;
    out->hits = g_hits;
    out->fallbacks = g_fallbacks;
    out->cache_bytes = g_cache_bytes;
    out->last_us = g_last_us;
    out->last_m = g_last_m;
    out->last_n = g_last_n;
    out->last_k = g_last_k;
    snprintf(out->status, sizeof(out->status), "%s", g_status);
}

const char *ps4_gnm_status_line(void) {
    return g_status;
}
