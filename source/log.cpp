#include "aether/log.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>

#include "aether/platform.h"

static pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;
static std::string g_log;

void logln(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    sceKernelDebugOutText(0, "LLM ");
    sceKernelDebugOutText(0, buf);
    sceKernelDebugOutText(0, "\n");
    pthread_mutex_lock(&g_log_lock);
    g_log += buf;
    g_log += "\n";
    if (g_log.size() > 16384) g_log.erase(0, g_log.size() - 16384);
    pthread_mutex_unlock(&g_log_lock);
}

std::string log_text()
{
    pthread_mutex_lock(&g_log_lock);
    std::string out = g_log;
    pthread_mutex_unlock(&g_log_lock);
    return out;
}

std::string log_tail(size_t max_len)
{
    pthread_mutex_lock(&g_log_lock);
    std::string out = g_log.size() > max_len ? g_log.substr(g_log.size() - max_len) : g_log;
    pthread_mutex_unlock(&g_log_lock);
    if (out.empty()) out = "No logs yet.";
    return out;
}

void on_llama_log(ggml_log_level, const char *text, void *)
{
    logln("%s", text);
}
