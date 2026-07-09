#include "aether/config.h"

#include <ctype.h>
#include <pthread.h>

static pthread_mutex_t g_config_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_default_max_tokens = 64;
static float g_default_temp = 0.7f;
static int g_api_type = API_OPENAI;

const char *api_type_name(int api_type)
{
    return api_type == API_ANTHROPIC ? "anthropic" : "openai";
}

int parse_api_type(const std::string &s, int fallback)
{
    if (s.empty()) return fallback;
    char c = (char)tolower((unsigned char)s[0]);
    if (c == 'a') return API_ANTHROPIC;
    if (c == 'o') return API_OPENAI;
    return fallback;
}

void get_config(int *max_tokens, float *temp)
{
    pthread_mutex_lock(&g_config_lock);
    *max_tokens = g_default_max_tokens;
    *temp = g_default_temp;
    pthread_mutex_unlock(&g_config_lock);
}

int get_api_type()
{
    pthread_mutex_lock(&g_config_lock);
    int api_type = g_api_type;
    pthread_mutex_unlock(&g_config_lock);
    return api_type;
}

void set_config(int max_tokens, float temp, int api_type)
{
    if (max_tokens < 1) max_tokens = 1;
    if (max_tokens > 2048) max_tokens = 2048;
    if (temp < 0.0f) temp = 0.0f;
    if (temp > 2.0f) temp = 2.0f;
    if (api_type != API_ANTHROPIC) api_type = API_OPENAI;
    pthread_mutex_lock(&g_config_lock);
    g_default_max_tokens = max_tokens;
    g_default_temp = temp;
    g_api_type = api_type;
    pthread_mutex_unlock(&g_config_lock);
}

void cycle_config_value(int field, int dir)
{
    static const int token_values[] = {64, 128, 256, 512, 1024};
    static const float temp_values[] = {0.0f, 0.4f, 0.7f, 1.0f, 1.3f};
    pthread_mutex_lock(&g_config_lock);
    if (field == 0) {
        int idx = 2;
        for (unsigned i = 0; i < sizeof(token_values) / sizeof(token_values[0]); i++) {
            if (token_values[i] == g_default_max_tokens) idx = (int)i;
        }
        int count = (int)(sizeof(token_values) / sizeof(token_values[0]));
        g_default_max_tokens = token_values[(idx + dir + count) % count];
    } else if (field == 1) {
        int idx = 2;
        for (unsigned i = 0; i < sizeof(temp_values) / sizeof(temp_values[0]); i++) {
            if (temp_values[i] == g_default_temp) idx = (int)i;
        }
        int count = (int)(sizeof(temp_values) / sizeof(temp_values[0]));
        g_default_temp = temp_values[(idx + dir + count) % count];
    } else {
        g_api_type = g_api_type == API_ANTHROPIC ? API_OPENAI : API_ANTHROPIC;
    }
    pthread_mutex_unlock(&g_config_lock);
}
