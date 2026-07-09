#pragma once

#include <stddef.h>

#include <string>

#include "ggml.h"

void logln(const char *fmt, ...);
std::string log_text();
std::string log_tail(size_t max_len);
void on_llama_log(ggml_log_level level, const char *text, void *user_data);
