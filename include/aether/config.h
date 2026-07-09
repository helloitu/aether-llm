#pragma once

#include <string>

enum ApiType {
    API_OPENAI = 0,
    API_ANTHROPIC = 1
};

const char *api_type_name(int api_type);
int parse_api_type(const std::string &s, int fallback);
void get_config(int *max_tokens, float *temp);
int get_api_type();
void set_config(int max_tokens, float temp, int api_type);
void cycle_config_value(int field, int dir);
