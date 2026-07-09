#pragma once

#include <pthread.h>
#include <stdint.h>

#include <string>
#include <vector>

#define JSON_NOEXCEPTION 1
#include "nlohmann/json.hpp"

extern pthread_mutex_t g_model_lock;

struct gen_metrics {
    int n_prompt = 0;
    int n_gen = 0;
    uint64_t prefill_us = 0;
    uint64_t ttft_us = 0;
    uint64_t decode_us = 0;
    uint64_t total_us = 0;
};

std::vector<std::string> list_models();
void unload_model_locked();
bool load_model_locked(const std::string &name);
bool start_ui_model_load(const std::string &name);
bool generate(const std::string &prompt, int max_tokens, float temp, std::string &out, gen_metrics &gm);
std::string apply_chat_template(const nlohmann::json &messages);
