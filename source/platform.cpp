#include "aether/platform.h"

#include <stdio.h>
#include <string.h>

#include "aether/app.h"

typedef int (*jailbreak_fn)(void);

double avail_dmem_mb()
{
    int64_t start = 0;
    size_t sz = 0;
    if (sceKernelAvailableDirectMemorySize(0, (int64_t)sceKernelGetDirectMemorySize(), 0x4000, &start, &sz) != 0) return -1.0;
    return (double)sz / (1024.0 * 1024.0);
}

void run_jailbreak()
{
    g_jb_h = (int)sceKernelLoadStartModule("/app0/Media/jb.prx", 0, NULL, 0, NULL, NULL);
    if (g_jb_h < 0) return;

    void *fp = NULL;
    g_jb_dlsym = sceKernelDlsym((uint32_t)g_jb_h, "jailbreak_me", &fp);
    if (g_jb_dlsym < 0 || !fp) return;

    g_jb_call = ((jailbreak_fn)fp)();
}

void get_ip()
{
    NetCtlInfoIP info;
    memset(&info, 0, sizeof(info));
    sceNetCtlGetInfo(ORBIS_NET_CTL_INFO_IP_ADDRESS, &info);
    if (info.ip_address[0]) snprintf(g_ip, sizeof(g_ip), "%s", info.ip_address);
}
