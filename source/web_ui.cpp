#include "aether/web_ui.h"

#include <fcntl.h>
#include <unistd.h>

#include "aether/app.h"

std::string load_web_ui()
{
    int fd = open(WEB_UI_PATH, O_RDONLY);
    if (fd < 0) return "<!doctype html><title>Aether</title><body style=\"background:#05020a;color:#f1e9ff;font-family:sans-serif\">Aether web UI missing</body>";
    std::string out;
    char buf[4096];
    while (1) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n <= 0) break;
        out.append(buf, (size_t)n);
    }
    close(fd);
    return out;
}
