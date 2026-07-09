#include "aether/http_server.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <exception>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <string>

#define JSON_NOEXCEPTION 1
#include "nlohmann/json.hpp"

#include "aether/app.h"
#include "aether/config.h"
#include "aether/log.h"
#include "aether/model.h"
#include "aether/platform.h"
#include "aether/web_ui.h"
#include "ps4_gnm_bridge.h"

using json = nlohmann::json;

static void write_all(int fd, const char *buf, size_t len)
{
    size_t off = 0;
    while (off < len) {
        ssize_t k = write(fd, buf + off, len - off);
        if (k <= 0) return;
        off += (size_t)k;
    }
}

static void send_resp(int fd, const char *status, const char *ctype, const std::string &body)
{
    char hdr[256];
    int n = snprintf(hdr, sizeof(hdr), "HTTP/1.1 %s\r\nContent-Type: %s\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Headers: *\r\nConnection: close\r\nContent-Length: %u\r\n\r\n", status, ctype, (unsigned)body.size());
    if (n > 0) write_all(fd, hdr, n);
    write_all(fd, body.data(), body.size());
}

static void send_json(int fd, const json &j)
{
    send_resp(fd, "200 OK", "application/json", j.dump(-1, ' ', false, json::error_handler_t::replace));
}

static bool read_request(int fd, std::string &head, std::string &body)
{
    std::string req;
    char buf[8192];
    size_t hend = std::string::npos;
    while (hend == std::string::npos) {
        int n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) return false;
        req.append(buf, n);
        hend = req.find("\r\n\r\n");
        if (req.size() > (1u << 20)) return false;
    }
    head = req.substr(0, hend);
    body = req.substr(hend + 4);
    size_t clen = 0;
    std::string lh = head;
    for (char &c : lh) c = (char)tolower((unsigned char)c);
    size_t p = lh.find("content-length:");
    if (p != std::string::npos) clen = (size_t)strtoul(lh.c_str() + p + 15, NULL, 10);
    while (body.size() < clen) {
        int n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        body.append(buf, n);
    }
    return true;
}

static void send_status(int fd)
{
    int max_tokens;
    float temp;
    get_config(&max_tokens, &temp);
    int api_type = get_api_type();
    ps4_gnm_stats gs;
    ps4_gnm_get_stats(&gs);
    json gpu = {
        {"ready", (bool)gs.ready},
        {"enabled", (bool)gs.enabled},
        {"offload_enabled", (bool)gs.offload_enabled},
        {"selftest_ok", (bool)gs.selftest_ok},
        {"last_error", gs.last_error},
        {"candidates", (uint64_t)gs.candidates},
        {"calls", (uint64_t)gs.calls},
        {"hits", (uint64_t)gs.hits},
        {"fallbacks", (uint64_t)gs.fallbacks},
        {"cache_mb", (double)gs.cache_bytes / (1024.0 * 1024.0)},
        {"last_us", (uint64_t)gs.last_us},
        {"last_m", gs.last_m},
        {"last_n", gs.last_n},
        {"last_k", gs.last_k},
        {"status", gs.status}
    };
    json ui = {
        {"sdl_init", g_dbg_sdl_init},
        {"win_ok", (bool)g_dbg_win_ok},
        {"ren_ok", (bool)g_dbg_ren_ok},
        {"frames", (uint64_t)g_dbg_frames},
        {"fps", (uint64_t)g_dbg_fps},
        {"frame_ms", (uint64_t)g_dbg_frame_ms},
        {"pad_handle", g_dbg_pad_handle},
        {"pad_read", g_dbg_pad_read},
        {"pad_connected", (bool)g_dbg_pad_connected},
        {"pad_buttons", (uint64_t)g_dbg_pad_buttons},
        {"last_button", g_dbg_last_button},
        {"screen", g_dbg_ui_screen},
        {"action", g_dbg_ui_action},
        {"focus", g_dbg_ui_focus ? "tab" : "menu"},
        {"error", g_dbg_err}
    };
    json j = {
        {"ok", true},
        {"version", APP_VERSION},
        {"ip", g_ip},
        {"port", LLM_PORT},
        {"model_loaded", (bool)g_model_loaded},
        {"loaded_model", g_loaded_name},
        {"model_loading", (bool)g_model_loading},
        {"generation_busy", (bool)g_generation_busy},
        {"requests", (uint64_t)g_requests},
        {"model_dir", MODEL_DIR},
        {"default_max_tokens", max_tokens},
        {"default_temperature", temp},
        {"api_type", api_type_name(api_type)},
        {"gpu", gpu},
        {"ui", ui}
    };
    send_json(fd, j);
}

static void send_models(int fd)
{
    json data = json::array();
    for (auto &m : list_models()) data.push_back({{"id", m}, {"object", "model"}, {"created", 0}, {"owned_by", "local"}});
    send_json(fd, {{"object", "list"}, {"data", data}});
}

static void handle_config(int fd, const std::string &body)
{
    json b = json::parse(body, nullptr, false);
    if (!b.is_object()) {
        send_resp(fd, "400 Bad Request", "application/json", "{\"error\":\"bad json\"}");
        return;
    }
    int max_tokens;
    float temp;
    get_config(&max_tokens, &temp);
    int api_type = get_api_type();
    if (b.contains("max_tokens")) max_tokens = b.value("max_tokens", max_tokens);
    if (b.contains("temperature")) temp = b.value("temperature", temp);
    api_type = parse_api_type(b.value("api_type", std::string("")), api_type);
    set_config(max_tokens, temp, api_type);
    get_config(&max_tokens, &temp);
    api_type = get_api_type();
    logln("config max_tokens=%d temp=%.2f api=%s", max_tokens, temp, api_type_name(api_type));
    send_json(fd, {{"ok", true}, {"default_max_tokens", max_tokens}, {"default_temperature", temp}, {"api_type", api_type_name(api_type)}});
}

static void handle_load(int fd, const std::string &body)
{
    json b = json::parse(body, nullptr, false);
    std::string name = b.is_object() ? b.value("model", std::string("")) : "";
    if (name.empty()) {
        send_resp(fd, "400 Bad Request", "application/json", "{\"error\":\"missing model\"}");
        return;
    }
    pthread_mutex_lock(&g_model_lock);
    bool ok = false;
    try {
        ok = load_model_locked(name);
    } catch (const std::exception &e) {
        logln("load exception: %s", e.what());
    }
    pthread_mutex_unlock(&g_model_lock);
    if (ok) send_json(fd, {{"ok", true}, {"loaded_model", g_loaded_name}});
    else send_resp(fd, "500 Internal Server Error", "application/json", "{\"ok\":false,\"error\":\"load failed\"}");
}

static void handle_generation(int fd, const std::string &path, const std::string &body)
{
    json b = json::parse(body, nullptr, false);
    if (!b.is_object()) {
        send_resp(fd, "400 Bad Request", "application/json", "{\"error\":\"bad json\"}");
        return;
    }
    bool anthropic = path == "/v1/messages";
    int default_max_tokens;
    float default_temp;
    get_config(&default_max_tokens, &default_temp);
    int max_tokens = b.value("max_tokens", default_max_tokens);
    float temp = b.value("temperature", default_temp);
    if (max_tokens < 1) max_tokens = 1;
    if (max_tokens > 2048) max_tokens = 2048;
    if (temp < 0.0f) temp = 0.0f;
    if (temp > 2.0f) temp = 2.0f;
    std::string want = b.value("model", std::string(""));

    bool model_locked = true;
    pthread_mutex_lock(&g_model_lock);
    try {
        if (!want.empty() && want != g_loaded_name) load_model_locked(want);
        if (!g_model_loaded) {
            pthread_mutex_unlock(&g_model_lock);
            model_locked = false;
            send_resp(fd, "400 Bad Request", "application/json", "{\"error\":\"no model loaded; POST /load or pass model\"}");
            return;
        }

        std::string prompt;
        if ((path == "/v1/chat/completions" || anthropic) && b.contains("messages") && b["messages"].is_array()) prompt = apply_chat_template(b["messages"]);
        else prompt = b.value("prompt", std::string(""));

        std::string out;
        gen_metrics gm;
        ps4_gnm_stats gs0;
        ps4_gnm_get_stats(&gs0);
        g_generation_busy = 1;
        logln("generate begin max_tokens=%d temp=%.2f prompt_bytes=%u", max_tokens, temp, (unsigned)prompt.size());
        bool ok = generate(prompt, max_tokens, temp, out, gm);
        double tg = gm.decode_us ? (gm.n_gen * 1e6 / (double)gm.decode_us) : 0.0;
        logln("generate end ok=%d prompt_tokens=%d generated=%d total_ms=%llu tg_tok_s=%.2f", ok ? 1 : 0, gm.n_prompt, gm.n_gen, (unsigned long long)(gm.total_us / 1000), tg);
        g_generation_busy = 0;
        ps4_gnm_stats gs1;
        ps4_gnm_get_stats(&gs1);
        double mem_mb = avail_dmem_mb();
        long long model_bytes = g_model_bytes;
        std::string mdl = g_loaded_name;
        pthread_mutex_unlock(&g_model_lock);
        model_locked = false;

        if (!ok) {
            send_resp(fd, "500 Internal Server Error", "application/json", "{\"error\":\"generation failed\"}");
            return;
        }

        double pp_tok_s = gm.prefill_us ? (gm.n_prompt * 1e6 / (double)gm.prefill_us) : 0.0;
        double tpot_ms = gm.n_gen ? (gm.decode_us / 1000.0 / gm.n_gen) : 0.0;
        double eff_bw_gbs = (double)model_bytes * tg / 1e9;
        json timing = {
            {"prompt_tokens", gm.n_prompt},
            {"gen_tokens", gm.n_gen},
            {"prefill_us", gm.prefill_us},
            {"ttft_us", gm.ttft_us},
            {"decode_us", gm.decode_us},
            {"total_us", gm.total_us},
            {"pp_tok_s", pp_tok_s},
            {"tg_tok_s", tg},
            {"tpot_ms", tpot_ms},
            {"eff_bw_gbs", eff_bw_gbs},
            {"model_mb", model_bytes / (1024.0 * 1024.0)},
            {"avail_dmem_mb", mem_mb},
            {"gpu_calls", (uint64_t)(gs1.calls - gs0.calls)},
            {"gpu_hits", (uint64_t)(gs1.hits - gs0.hits)},
            {"gpu_fallbacks", (uint64_t)(gs1.fallbacks - gs0.fallbacks)}
        };
        json usage = {{"prompt_tokens", gm.n_prompt}, {"completion_tokens", gm.n_gen}, {"total_tokens", gm.n_prompt + gm.n_gen}};
        if (anthropic) {
            send_json(fd, {{"id", "msg-ps4"}, {"type", "message"}, {"role", "assistant"}, {"model", mdl}, {"stop_reason", "end_turn"}, {"stop_sequence", nullptr}, {"content", json::array({{{"type", "text"}, {"text", out}}})}, {"usage", {{"input_tokens", gm.n_prompt}, {"output_tokens", gm.n_gen}}}, {"timing", timing}});
        } else if (path == "/v1/chat/completions") {
            send_json(fd, {{"id", "chatcmpl-ps4"}, {"object", "chat.completion"}, {"created", 0}, {"model", mdl}, {"usage", usage}, {"timing", timing}, {"choices", json::array({{{"index", 0}, {"finish_reason", "stop"}, {"message", {{"role", "assistant"}, {"content", out}}}}})}});
        } else {
            send_json(fd, {{"id", "cmpl-ps4"}, {"object", "text_completion"}, {"created", 0}, {"model", mdl}, {"usage", usage}, {"timing", timing}, {"choices", json::array({{{"index", 0}, {"finish_reason", "stop"}, {"text", out}}})}});
        }
    } catch (const std::exception &e) {
        if (model_locked) pthread_mutex_unlock(&g_model_lock);
        g_generation_busy = 0;
        logln("chat request exception: %s", e.what());
        send_resp(fd, "500 Internal Server Error", "application/json", "{\"error\":\"request exception\"}");
    }
}

static void handle(int fd, const std::string &method, const std::string &path, const std::string &body)
{
    if (method == "OPTIONS") {
        send_resp(fd, "204 No Content", "text/plain", "");
        return;
    }
    if (path == "/" || path == "/index.html") {
        send_resp(fd, "200 OK", "text/html; charset=utf-8", load_web_ui());
        return;
    }
    if (path == "/logs") {
        send_resp(fd, "200 OK", "text/plain; charset=utf-8", log_text());
        return;
    }
    if (path == "/status" || path == "/health") {
        send_status(fd);
        return;
    }
    if (path == "/gpu-test") {
        int ok = ps4_gnm_selftest();
        ps4_gnm_stats gs;
        ps4_gnm_get_stats(&gs);
        send_json(fd, {{"ok", (bool)ok}, {"selftest_ok", (bool)gs.selftest_ok}, {"ready", (bool)gs.ready}, {"last_error", gs.last_error}, {"status", gs.status}});
        return;
    }
    if (path == "/gpu/offload" && method == "POST") {
        json b = json::parse(body, nullptr, false);
        int enabled = b.is_object() ? (b.value("enabled", false) ? 1 : 0) : 0;
        ps4_gnm_set_offload_enabled(enabled);
        ps4_gnm_stats gs;
        ps4_gnm_get_stats(&gs);
        send_json(fd, {{"ok", true}, {"offload_enabled", (bool)gs.offload_enabled}, {"status", gs.status}});
        return;
    }
    if (path == "/v1/models" || path == "/models") {
        send_models(fd);
        return;
    }
    if (path == "/unload" && method == "POST") {
        pthread_mutex_lock(&g_model_lock);
        unload_model_locked();
        pthread_mutex_unlock(&g_model_lock);
        send_json(fd, {{"ok", true}, {"model_loaded", false}});
        return;
    }
    if (path == "/config" && method == "POST") {
        handle_config(fd, body);
        return;
    }
    if (path == "/load" && method == "POST") {
        handle_load(fd, body);
        return;
    }
    if ((path == "/v1/chat/completions" || path == "/v1/completions" || path == "/v1/messages") && method == "POST") {
        handle_generation(fd, path, body);
        return;
    }
    send_resp(fd, "404 Not Found", "application/json", "{\"error\":\"not found\"}");
}

static void serve_client(int c)
{
    struct linger lg = {1, 1};
    setsockopt(c, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));

    std::string head;
    std::string body;
    if (read_request(c, head, body)) {
        std::string method = head.substr(0, head.find(' '));
        size_t s = head.find(' ');
        size_t e = head.find(' ', s + 1);
        std::string path = (s != std::string::npos && e != std::string::npos) ? head.substr(s + 1, e - s - 1) : "/";
        size_t q = path.find('?');
        if (q != std::string::npos) path = path.substr(0, q);
        g_requests++;
        logln("%s %s", method.c_str(), path.c_str());
        try {
            handle(c, method, path, body);
        } catch (const std::exception &e) {
            logln("handler exception: %s", e.what());
        }
    }
    shutdown(c, SHUT_RDWR);
    close(c);
}

static void *client_main(void *arg)
{
    int c = *(int *)arg;
    free(arg);
    serve_client(c);
    return NULL;
}

void *server_main(void *)
{
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(LLM_PORT);
    int sock = -1;
    for (int attempt = 0; attempt < 60 && !g_stop_server; attempt++) {
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            logln("socket failed errno=%d", errno);
            usleep(250000);
            continue;
        }
        int yes = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0) break;
        if (attempt == 0 || attempt % 8 == 0) logln("bind :%d failed errno=%d (attempt %d), retrying", LLM_PORT, errno, attempt + 1);
        close(sock);
        sock = -1;
        usleep(250000);
    }
    if (sock < 0) {
        logln("bind gave up after retries");
        return NULL;
    }
    if (listen(sock, 8) < 0) {
        logln("listen failed errno=%d", errno);
        close(sock);
        return NULL;
    }
    g_listen_fd = sock;
    g_server_listening = 1;
    logln("HTTP server listening on :%d", LLM_PORT);

    while (!g_stop_server) {
        struct sockaddr_in cli;
        socklen_t cl = sizeof(cli);
        int c = accept(sock, (struct sockaddr *)&cli, &cl);
        if (g_stop_server) break;
        if (c < 0) continue;
        int *pc = (int *)malloc(sizeof(int));
        pthread_t client;
        if (pc) *pc = c;
        if (!pc || pthread_create(&client, NULL, client_main, pc) != 0) {
            if (pc) free(pc);
            serve_client(c);
        } else {
            pthread_detach(client);
        }
    }
    g_server_listening = 0;
    g_listen_fd = -1;
    close(sock);
    return NULL;
}
