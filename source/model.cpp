#include "aether/model.h"

#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "aether/app.h"
#include "aether/log.h"
#include "aether/platform.h"
#include "llama.h"
#include "ps4_gnm_bridge.h"

pthread_mutex_t g_model_lock = PTHREAD_MUTEX_INITIALIZER;

static llama_model *g_model = NULL;
static llama_context *g_ctx = NULL;
static const llama_vocab *g_vocab = NULL;

std::vector<std::string> list_models()
{
    std::vector<std::string> out;
    DIR *d = opendir(MODEL_DIR);
    if (!d) return out;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        std::string n = e->d_name;
        if (n.size() > 5 && n.substr(n.size() - 5) == ".gguf") out.push_back(n);
    }
    closedir(d);
    return out;
}

void unload_model_locked()
{
    if (g_ctx) {
        llama_free(g_ctx);
        g_ctx = NULL;
    }
    if (g_model) {
        llama_model_free(g_model);
        g_model = NULL;
    }
    ps4_gnm_clear_cache();
    g_vocab = NULL;
    g_model_loaded = 0;
    g_model_bytes = 0;
    g_loaded_name[0] = 0;
}

bool load_model_locked(const std::string &name)
{
    unload_model_locked();
    std::string path = std::string(MODEL_DIR) + "/" + name;
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        logln("model not found: %s", path.c_str());
        return false;
    }

    logln("loading %s (%lld bytes)", name.c_str(), (long long)st.st_size);
    g_model_bytes = (long long)st.st_size;
    llama_model_params mp = llama_model_default_params();
    mp.n_gpu_layers = 0;
    mp.use_mmap = true;
    mp.use_mlock = false;
    mp.use_direct_io = false;
    logln("llama mmap support=%d use_mmap=%d mlock=%d", llama_supports_mmap() ? 1 : 0, mp.use_mmap ? 1 : 0, mp.use_mlock ? 1 : 0);
    if (st.st_size > (off_t)(4ull << 30) && !llama_supports_mmap()) {
        logln("load failed: mmap unsupported for >4 GB model");
        return false;
    }

    g_model = llama_model_load_from_file(path.c_str(), mp);
    if (!g_model) {
        logln("load failed: %s", name.c_str());
        return false;
    }

    llama_context_params cp = llama_context_default_params();
    cp.n_ctx = 2048;
    cp.n_batch = 128;
    cp.n_threads = 6;
    cp.n_threads_batch = 6;
    g_ctx = llama_init_from_model(g_model, cp);
    if (!g_ctx) {
        llama_model_free(g_model);
        g_model = NULL;
        logln("ctx failed");
        return false;
    }

    g_vocab = llama_model_get_vocab(g_model);
    snprintf(g_loaded_name, sizeof(g_loaded_name), "%s", name.c_str());
    g_model_loaded = 1;
    logln("loaded %s", name.c_str());
    return true;
}

struct LoadJob {
    char name[256];
};

static void *ui_load_model_thread(void *arg)
{
    LoadJob *job = (LoadJob *)arg;
    std::string name = job->name;
    free(job);
    pthread_mutex_lock(&g_model_lock);
    bool ok = load_model_locked(name);
    pthread_mutex_unlock(&g_model_lock);
    g_model_loading = 0;
    logln("async UI load %s: %s", name.c_str(), ok ? "ok" : "failed");
    return NULL;
}

bool start_ui_model_load(const std::string &name)
{
    if (g_model_loading) return false;
    LoadJob *job = (LoadJob *)calloc(1, sizeof(LoadJob));
    if (!job) return false;
    snprintf(job->name, sizeof(job->name), "%s", name.c_str());
    g_model_loading = 1;
    pthread_t th;
    if (pthread_create(&th, NULL, ui_load_model_thread, job) != 0) {
        g_model_loading = 0;
        free(job);
        return false;
    }
    pthread_detach(th);
    return true;
}

bool generate(const std::string &prompt, int max_tokens, float temp, std::string &out, gen_metrics &gm)
{
    const uint64_t t0 = sceKernelGetProcessTime();
    llama_memory_clear(llama_get_memory(g_ctx), true);

    std::vector<llama_token> toks(prompt.size() + 16);
    int n = llama_tokenize(g_vocab, prompt.c_str(), (int)prompt.size(), toks.data(), (int)toks.size(), true, true);
    if (n < 0) {
        toks.resize(-n);
        n = llama_tokenize(g_vocab, prompt.c_str(), (int)prompt.size(), toks.data(), (int)toks.size(), true, true);
    }
    if (n <= 0) return false;
    toks.resize(n);

    const int n_ctx = (int)llama_n_ctx(g_ctx);
    const int n_batch = (int)llama_n_batch(g_ctx);
    int max_prompt = n_ctx - max_tokens;
    if (max_prompt < 1) max_prompt = 1;
    if (n > max_prompt) {
        n = max_prompt;
        toks.resize(n);
    }
    gm.n_prompt = n;
    int room = n_ctx - n;
    if (max_tokens > room) max_tokens = room;

    for (int i = 0; i < n; i += n_batch) {
        int cur = n - i < n_batch ? n - i : n_batch;
        llama_batch batch = llama_batch_get_one(toks.data() + i, cur);
        if (llama_decode(g_ctx, batch) != 0) return false;
    }
    const uint64_t t_prefill = sceKernelGetProcessTime();
    gm.prefill_us = t_prefill - t0;

    llama_sampler *smpl = llama_sampler_chain_init(llama_sampler_chain_default_params());
    if (temp <= 0.0f) {
        llama_sampler_chain_add(smpl, llama_sampler_init_greedy());
    } else {
        llama_sampler_chain_add(smpl, llama_sampler_init_top_k(40));
        llama_sampler_chain_add(smpl, llama_sampler_init_top_p(0.95f, 1));
        llama_sampler_chain_add(smpl, llama_sampler_init_temp(temp));
        llama_sampler_chain_add(smpl, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));
    }

    gm.n_gen = 0;
    char piece[256];
    for (int i = 0; i < max_tokens; i++) {
        llama_token tok = llama_sampler_sample(smpl, g_ctx, -1);
        if (llama_vocab_is_eog(g_vocab, tok)) break;
        int pn = llama_token_to_piece(g_vocab, tok, piece, (int)sizeof(piece), 0, true);
        if (pn < 0) break;
        out.append(piece, pn);
        gm.n_gen++;
        if (gm.n_gen == 1) gm.ttft_us = sceKernelGetProcessTime() - t0;
        llama_batch b = llama_batch_get_one(&tok, 1);
        if (llama_decode(g_ctx, b) != 0) break;
    }
    llama_sampler_free(smpl);
    const uint64_t t_end = sceKernelGetProcessTime();
    gm.decode_us = t_end - t_prefill;
    gm.total_us = t_end - t0;
    return true;
}

std::string apply_chat_template(const nlohmann::json &messages)
{
    std::string out;
    for (const auto &m : messages) {
        if (!m.is_object()) continue;
        std::string role = m.value("role", std::string("user"));
        std::string content = m.value("content", std::string(""));
        out += "<|im_start|>" + role + "\n" + content + "<|im_end|>\n";
    }
    out += "<|im_start|>assistant\n";
    return out;
}
