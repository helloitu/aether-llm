#pragma once

#include <stddef.h>
#include <stdint.h>

extern "C" int sceSysmoduleLoadModule(uint16_t id);
extern "C" int sceSysmoduleLoadModuleInternal(uint32_t id);
extern "C" int sceSystemServiceHideSplashScreen();
extern "C" int sceSystemServiceLoadExec(const char *path, char *const argv[]);
extern "C" int sceKernelDebugOutText(int ch, const char *text, ...);
extern "C" uint32_t sceKernelLoadStartModule(const char *, size_t, const void *, uint32_t, void *, void *);
extern "C" int sceKernelDlsym(uint32_t handle, const char *sym, void **addr);
extern "C" void sceNetCtlInit();
extern "C" void sceNetCtlGetInfo(int code, void *info);
extern "C" uint64_t sceKernelGetProcessTime(void);
extern "C" size_t sceKernelGetDirectMemorySize(void);
extern "C" int sceKernelAvailableDirectMemorySize(int64_t, int64_t, size_t, int64_t *, size_t *);

double avail_dmem_mb();
void run_jailbreak();
void get_ip();
