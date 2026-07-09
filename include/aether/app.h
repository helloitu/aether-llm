#pragma once

#include <stdint.h>

#define APP_VERSION "0.88"
#define APP_HOME "/user/app/LLMT00001"
#define MODEL_DIR "/user/data/llm_models"
#define WEB_UI_PATH "/app0/web/index.html"
#define LLM_PORT 8080

#define ORBIS_SYSMODULE_MESSAGE_DIALOG 0x00A4
#define SCE_SYSMODULE_INTERNAL_SYSTEM_SERVICE 0x80000010
#define SCE_SYSMODULE_INTERNAL_USER_SERVICE 0x80000011
#define SCE_SYSMODULE_INTERNAL_COMMON_DIALOG 0x80000018
#define SCE_SYSMODULE_INTERNAL_NETCTL 0x80000009
#define SCE_SYSMODULE_INTERNAL_NET 0x8000001C
#define SCE_SYSMODULE_INTERNAL_PAD 0x80000024
#define ORBIS_NET_CTL_INFO_IP_ADDRESS 14

typedef struct {
    char ip_address[16];
    char rest[512];
} NetCtlInfoIP;

extern volatile int g_server_listening;
extern volatile int g_model_loaded;
extern volatile int g_model_loading;
extern volatile int g_generation_busy;
extern volatile int g_stop_server;
extern volatile int g_listen_fd;
extern volatile uint64_t g_requests;
extern volatile long long g_model_bytes;
extern char g_loaded_name[128];
extern char g_ip[64];
extern volatile int g_jb_h;
extern volatile int g_jb_dlsym;
extern volatile int g_jb_call;
extern volatile int g_dbg_mod_video;
extern volatile int g_dbg_mod_piglet;
extern volatile int g_dbg_mod_pad;
extern volatile int g_dbg_sdl_init;
extern volatile int g_dbg_win_ok;
extern volatile int g_dbg_ren_ok;
extern volatile int g_dbg_img_ok;
extern volatile int g_dbg_img_w;
extern volatile int g_dbg_img_h;
extern volatile int g_dbg_last_button;
extern volatile int g_dbg_pad_handle;
extern volatile int g_dbg_pad_read;
extern volatile int g_dbg_pad_connected;
extern volatile unsigned g_dbg_pad_buttons;
extern volatile unsigned g_dbg_frames;
extern volatile unsigned g_dbg_fps;
extern volatile unsigned g_dbg_frame_ms;
extern volatile int g_dbg_ui_screen;
extern volatile int g_dbg_ui_action;
extern volatile int g_dbg_ui_focus;
extern char g_dbg_err[256];
extern int g_pad_handle;
extern unsigned g_pad_prev;
