#include "aether/dialog_ui.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "MsgDialog.h"
#include "aether/app.h"
#include "aether/config.h"
#include "aether/log.h"
#include "aether/model.h"

int show_buttons(const char *msg, const char *button1, const char *button2)
{
    sceMsgDialogTerminate();
    sceMsgDialogInitialize();
    OrbisMsgDialogParam p;
    OrbisMsgDialogParamInitialize(&p);
    p.mode = ORBIS_MSG_DIALOG_MODE_USER_MSG;
    OrbisMsgDialogButtonsParam buttons;
    memset(&buttons, 0, sizeof(buttons));
    buttons.msg1 = button1;
    buttons.msg2 = button2;
    OrbisMsgDialogUserMessageParam u;
    memset(&u, 0, sizeof(u));
    u.msg = msg;
    u.buttonType = button2 ? ORBIS_MSG_DIALOG_BUTTON_TYPE_2BUTTONS : ORBIS_MSG_DIALOG_BUTTON_TYPE_OK;
    u.buttonsParam = button2 ? &buttons : NULL;
    p.userMsgParam = &u;
    if (sceMsgDialogOpen(&p) != 0) return ORBIS_COMMON_DIALOG_RESULT_USER_CANCELED;
    OrbisMsgDialogResult r;
    while (1) {
        if (sceMsgDialogUpdateStatus() == ORBIS_COMMON_DIALOG_STATUS_FINISHED) {
            memset(&r, 0, sizeof(r));
            sceMsgDialogGetResult(&r);
            sceMsgDialogTerminate();
            return r.buttonId;
        }
        usleep(20000);
    }
}

void show_ok(const char *msg)
{
    show_buttons(msg, "OK", NULL);
}

void show_wait(const char *msg)
{
    sceMsgDialogTerminate();
    sceMsgDialogInitialize();
    OrbisMsgDialogParam p;
    OrbisMsgDialogParamInitialize(&p);
    p.mode = ORBIS_MSG_DIALOG_MODE_USER_MSG;
    OrbisMsgDialogUserMessageParam u;
    memset(&u, 0, sizeof(u));
    u.msg = msg;
    u.buttonType = ORBIS_MSG_DIALOG_BUTTON_TYPE_WAIT;
    p.userMsgParam = &u;
    sceMsgDialogOpen(&p);
}

static void show_logs()
{
    std::string tail = log_tail(1200);
    std::string msg = "Aether logs\n\n" + tail;
    show_ok(msg.c_str());
}

static void show_config()
{
    int field = 0;
    while (1) {
        int mt;
        float tp;
        get_config(&mt, &tp);
        int api_type = get_api_type();
        char msg[512];
        snprintf(msg, sizeof(msg), "Aether config\n\n%s max_tokens: %d\n%s temperature: %.1f\n%s api_type: %s\n\nChange edits the selected value.\nNext moves selection; close dialog to return.", field == 0 ? ">" : " ", mt, field == 1 ? ">" : " ", tp, field == 2 ? ">" : " ", api_type_name(api_type));
        int btn = show_buttons(msg, "Change", "Next");
        if (btn == ORBIS_MSG_DIALOG_BUTTON_ID_OK) {
            cycle_config_value(field, 1);
        } else if (btn == ORBIS_MSG_DIALOG_BUTTON_ID_NO) {
            field = (field + 1) % 3;
        } else {
            return;
        }
    }
}

static void show_models()
{
    std::vector<std::string> models = list_models();
    if (g_model_loaded) models.insert(models.begin(), "[Unload current model]");
    if (models.empty()) {
        show_ok("Aether models\n\nNo .gguf files found in:\n" MODEL_DIR);
        return;
    }

    size_t selected = 0;
    while (1) {
        char msg[1400];
        int n = snprintf(msg, sizeof(msg), "Aether models\n\nCurrent: %s\nDir: %s\n\n", g_model_loaded ? g_loaded_name : "none", MODEL_DIR);
        for (size_t i = 0; i < models.size() && n < (int)sizeof(msg) - 80; i++) {
            n += snprintf(msg + n, sizeof(msg) - n, "%c %s\n", i == selected ? '>' : ' ', models[i].c_str());
        }
        snprintf(msg + n, sizeof(msg) - n, "\nLoad/Unload selects. Next moves.");
        int btn = show_buttons(msg, g_model_loaded && selected == 0 ? "Unload" : "Load", "Next");
        if (btn == ORBIS_MSG_DIALOG_BUTTON_ID_OK) {
            if (g_model_loaded && selected == 0) {
                pthread_mutex_lock(&g_model_lock);
                unload_model_locked();
                pthread_mutex_unlock(&g_model_lock);
                logln("model unloaded from PS4 UI");
                show_ok("Model unloaded.");
                return;
            }
            std::string model = models[selected];
            show_wait("Loading model...\n\nThis can take a while on PS4.");
            pthread_mutex_lock(&g_model_lock);
            bool ok = load_model_locked(model);
            pthread_mutex_unlock(&g_model_lock);
            sceMsgDialogTerminate();
            char done[512];
            snprintf(done, sizeof(done), "%s\n\n%s", ok ? "Model loaded." : "Model load failed.", model.c_str());
            show_ok(done);
            return;
        }
        if (btn == ORBIS_MSG_DIALOG_BUTTON_ID_NO) selected = (selected + 1) % models.size();
        else return;
    }
}

void run_dialog_ui()
{
    const char *items[] = {"Status", "Models", "Logs", "Config", "Exit"};
    int selected = 0;
    while (1) {
        int mt;
        float tp;
        get_config(&mt, &tp);
        char msg[1024];
        snprintf(msg, sizeof(msg), "Aether v" APP_VERSION "\n\nHTTP: http://%s:%d/\nAPI:  http://%s:%d/v1\nServer: %s\nModel: %s\nDefaults: %d tokens, temp %.1f\nRequests: %llu\n\n> %s\n\nOpen selects. Next moves.", g_ip, LLM_PORT, g_ip, LLM_PORT, g_server_listening ? "listening" : "not listening", g_model_loaded ? g_loaded_name : "none", mt, tp, (unsigned long long)g_requests, items[selected]);
        int btn = show_buttons(msg, "Open", "Next");
        if (btn == ORBIS_MSG_DIALOG_BUTTON_ID_NO) {
            selected = (selected + 1) % (sizeof(items) / sizeof(items[0]));
            continue;
        }
        if (btn != ORBIS_MSG_DIALOG_BUTTON_ID_OK) break;
        if (selected == 0) show_ok(msg);
        else if (selected == 1) show_models();
        else if (selected == 2) show_logs();
        else if (selected == 3) show_config();
        else break;
    }
}
