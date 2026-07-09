#include <pthread.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "CommonDialog.h"
#include "MsgDialog.h"
#include "aether/app.h"
#include "aether/http_server.h"
#include "aether/log.h"
#include "aether/model.h"
#include "aether/native_ui.h"
#include "aether/platform.h"
#include "llama.h"
#include "ps4_gnm_bridge.h"

extern "C" void _init(void) {}
extern "C" void _fini(void) {}

static_assert(sizeof(off_t) >= 8, "Aether needs 64-bit off_t for >4 GB GGUF files");
static_assert(sizeof(((struct stat *)0)->st_size) >= 8, "Aether needs 64-bit stat sizes");

int main(int, char **)
{
    run_jailbreak();
    mkdir(APP_HOME, 0777);
    mkdir(MODEL_DIR, 0777);
    sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_SYSTEM_SERVICE);
    sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_USER_SERVICE);
    sceSysmoduleLoadModule(ORBIS_SYSMODULE_MESSAGE_DIALOG);
    sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_NETCTL);
    sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_NET);
    sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_COMMON_DIALOG);
    sceCommonDialogInitialize();
    sceNetCtlInit();
    get_ip();
    sceSystemServiceHideSplashScreen();

    llama_log_set(on_llama_log, NULL);
    llama_backend_init();
    logln("Aether v%s starting", APP_VERSION);
    logln("jailbreak load=0x%08x dlsym=0x%08x call=0x%08x", g_jb_h, g_jb_dlsym, g_jb_call);
    ps4_gnm_init();
    int gnm_ok = ps4_gnm_selftest();
    logln("GNM bridge selftest=%d status=%s", gnm_ok, ps4_gnm_status_line());

    pthread_t th;
    int server_started = pthread_create(&th, NULL, server_main, NULL) == 0;
    if (!server_started) logln("HTTP server thread failed to start");
    sleep(1);
    run_ps4_ui();

    g_stop_server = 1;
    if (g_listen_fd >= 0) {
        shutdown((int)g_listen_fd, SHUT_RDWR);
        close((int)g_listen_fd);
    }
    if (server_started) pthread_join(th, NULL);
    pthread_mutex_lock(&g_model_lock);
    unload_model_locked();
    pthread_mutex_unlock(&g_model_lock);
    llama_backend_free();
    sceSystemServiceLoadExec("exit", NULL);
    return 0;
}
