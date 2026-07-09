#include "aether/native_ui.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include <string>
#include <vector>

#include <orbis/Pad.h>
#include <orbis/Pigletv2VSH.h>
#include <orbis/UserService.h>
#include "shaders/rects_vs.essl.h"
#include "shaders/rects_fs.essl.h"

#include "aether/app.h"
#include "aether/config.h"
#include "aether/dialog_ui.h"
#include "aether/log.h"
#include "aether/model.h"
#include "aether/platform.h"
#include "ps4_gnm_bridge.h"
enum UiScreen { UI_STATUS = 0, UI_MODELS, UI_LOGS, UI_CONFIG, UI_EXIT, UI_COUNT };
enum UiAction { ACT_NONE = 0, ACT_UP, ACT_DOWN, ACT_LEFT, ACT_RIGHT, ACT_SELECT, ACT_BACK, ACT_QUIT };
enum { CONFIG_FIELD_COUNT = 3 };
enum UiFocus { FOCUS_MENU = 0, FOCUS_TAB = 1 };

typedef uint32_t Uint32;
typedef struct { uint8_t r, g, b, a; } SDL_Color;
typedef struct { int x, y, w, h; } SDL_Rect;
typedef struct { int unused; } SDL_Window;
typedef struct {
	EGLDisplay display;
	EGLSurface surface;
	EGLContext context;
	GLuint program;
	GLint pos_loc;
	GLint color_loc;
	SDL_Color draw_color;
	SDL_Color batch_color;
	std::vector<GLfloat> batch_vertices;
} SDL_Renderer;

static void set_ui_error(const char *fmt, ...)
{
	va_list ap; va_start(ap, fmt); vsnprintf(g_dbg_err, sizeof(g_dbg_err), fmt, ap); va_end(ap);
	logln("Piglet UI: %s", g_dbg_err);
}

static void load_visual_modules()
{
	g_dbg_mod_video = (int)sceKernelLoadStartModule("/system/common/lib/libSceVideoOut.sprx", 0, NULL, 0, NULL, NULL);
	g_dbg_mod_piglet = (int)sceKernelLoadStartModule("/system/common/lib/libScePigletv2VSH.sprx", 0, NULL, 0, NULL, NULL);
	g_dbg_mod_pad = sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_PAD);
	logln("visual modules video=0x%08x piglet=0x%08x pad=0x%08x",
	      g_dbg_mod_video, g_dbg_mod_piglet, g_dbg_mod_pad);
}

static void init_direct_pad()
{
	int param = 700;
	sceUserServiceInitialize(&param);
	int user = 0x10000000;
	int urc = sceUserServiceGetInitialUser(&user);
	int irc = scePadInit();
	if (urc < 0) user = 0x10000000;
	g_pad_handle = scePadOpen(user, ORBIS_PAD_PORT_TYPE_STANDARD, 0, NULL);
	g_dbg_pad_handle = g_pad_handle;
	logln("pad init user_rc=0x%08x user=%d init=0x%08x handle=0x%08x",
	      urc, user, irc, g_pad_handle);
}

static uint8_t glyph_row(char c, int row)
{
	if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
#define GLYPH(a,b,c,d,e,f,g) do { static const uint8_t r[7] = {a,b,c,d,e,f,g}; return r[row]; } while (0)
	switch (c) {
	case '0': GLYPH(0x0e,0x11,0x13,0x15,0x19,0x11,0x0e);
	case '1': GLYPH(0x04,0x0c,0x04,0x04,0x04,0x04,0x0e);
	case '2': GLYPH(0x0e,0x11,0x01,0x02,0x04,0x08,0x1f);
	case '3': GLYPH(0x1e,0x01,0x01,0x0e,0x01,0x01,0x1e);
	case '4': GLYPH(0x02,0x06,0x0a,0x12,0x1f,0x02,0x02);
	case '5': GLYPH(0x1f,0x10,0x1e,0x01,0x01,0x11,0x0e);
	case '6': GLYPH(0x06,0x08,0x10,0x1e,0x11,0x11,0x0e);
	case '7': GLYPH(0x1f,0x01,0x02,0x04,0x08,0x08,0x08);
	case '8': GLYPH(0x0e,0x11,0x11,0x0e,0x11,0x11,0x0e);
	case '9': GLYPH(0x0e,0x11,0x11,0x0f,0x01,0x02,0x0c);
	case 'A': GLYPH(0x0e,0x11,0x11,0x1f,0x11,0x11,0x11);
	case 'B': GLYPH(0x1e,0x11,0x11,0x1e,0x11,0x11,0x1e);
	case 'C': GLYPH(0x0e,0x11,0x10,0x10,0x10,0x11,0x0e);
	case 'D': GLYPH(0x1e,0x11,0x11,0x11,0x11,0x11,0x1e);
	case 'E': GLYPH(0x1f,0x10,0x10,0x1e,0x10,0x10,0x1f);
	case 'F': GLYPH(0x1f,0x10,0x10,0x1e,0x10,0x10,0x10);
	case 'G': GLYPH(0x0e,0x11,0x10,0x17,0x11,0x11,0x0e);
	case 'H': GLYPH(0x11,0x11,0x11,0x1f,0x11,0x11,0x11);
	case 'I': GLYPH(0x0e,0x04,0x04,0x04,0x04,0x04,0x0e);
	case 'J': GLYPH(0x07,0x02,0x02,0x02,0x12,0x12,0x0c);
	case 'K': GLYPH(0x11,0x12,0x14,0x18,0x14,0x12,0x11);
	case 'L': GLYPH(0x10,0x10,0x10,0x10,0x10,0x10,0x1f);
	case 'M': GLYPH(0x11,0x1b,0x15,0x15,0x11,0x11,0x11);
	case 'N': GLYPH(0x11,0x19,0x15,0x13,0x11,0x11,0x11);
	case 'O': GLYPH(0x0e,0x11,0x11,0x11,0x11,0x11,0x0e);
	case 'P': GLYPH(0x1e,0x11,0x11,0x1e,0x10,0x10,0x10);
	case 'Q': GLYPH(0x0e,0x11,0x11,0x11,0x15,0x12,0x0d);
	case 'R': GLYPH(0x1e,0x11,0x11,0x1e,0x14,0x12,0x11);
	case 'S': GLYPH(0x0f,0x10,0x10,0x0e,0x01,0x01,0x1e);
	case 'T': GLYPH(0x1f,0x04,0x04,0x04,0x04,0x04,0x04);
	case 'U': GLYPH(0x11,0x11,0x11,0x11,0x11,0x11,0x0e);
	case 'V': GLYPH(0x11,0x11,0x11,0x11,0x11,0x0a,0x04);
	case 'W': GLYPH(0x11,0x11,0x11,0x15,0x15,0x15,0x0a);
	case 'X': GLYPH(0x11,0x11,0x0a,0x04,0x0a,0x11,0x11);
	case 'Y': GLYPH(0x11,0x11,0x0a,0x04,0x04,0x04,0x04);
	case 'Z': GLYPH(0x1f,0x01,0x02,0x04,0x08,0x10,0x1f);
	case '.': GLYPH(0,0,0,0,0,0x0c,0x0c);
	case ',': GLYPH(0,0,0,0,0,0x0c,0x08);
	case ':': GLYPH(0,0x0c,0x0c,0,0x0c,0x0c,0);
	case '/': GLYPH(0x01,0x02,0x02,0x04,0x08,0x08,0x10);
	case '-': GLYPH(0,0,0,0x1f,0,0,0);
	case '_': GLYPH(0,0,0,0,0,0,0x1f);
	case '[': GLYPH(0x0e,0x08,0x08,0x08,0x08,0x08,0x0e);
	case ']': GLYPH(0x0e,0x02,0x02,0x02,0x02,0x02,0x0e);
	case '(': GLYPH(0x02,0x04,0x08,0x08,0x08,0x04,0x02);
	case ')': GLYPH(0x08,0x04,0x02,0x02,0x02,0x04,0x08);
	case '<': GLYPH(0x02,0x04,0x08,0x10,0x08,0x04,0x02);
	case '>': GLYPH(0x08,0x04,0x02,0x01,0x02,0x04,0x08);
	case '=': GLYPH(0,0x1f,0,0x1f,0,0,0);
	case '@': GLYPH(0x0e,0x11,0x17,0x15,0x17,0x10,0x0e);
	case '#': GLYPH(0x0a,0x1f,0x0a,0x0a,0x1f,0x0a,0x0a);
	case '%': GLYPH(0x19,0x19,0x02,0x04,0x08,0x13,0x13);
	case '+': GLYPH(0,0x04,0x04,0x1f,0x04,0x04,0);
	case '?': GLYPH(0x0e,0x11,0x01,0x02,0x04,0,0x04);
	case '!': GLYPH(0x04,0x04,0x04,0x04,0x04,0,0x04);
	case ' ': GLYPH(0,0,0,0,0,0,0);
	}
#undef GLYPH
	return row == 6 ? 0x1f : (row == 0 || row == 3 ? 0x11 : 0x04);
}

static Uint32 SDL_GetTicks()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (Uint32)(tv.tv_sec * 1000u + tv.tv_usec / 1000u);
}

static void SDL_Delay(Uint32 ms)
{
	usleep(ms * 1000);
}

static GLuint gl_compile_binary(GLenum type, const char *data, int size)
{
	GLuint shader = glCreateShader(type);
	if (!shader) return 0;
	glShaderBinary(1, &shader, 0, (const GLvoid *)data, size);
	GLint ok = GL_FALSE;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (ok == GL_FALSE) {
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}

static GLuint gl_build_rect_program()
{
	GLuint vs = gl_compile_binary(GL_VERTEX_SHADER, rects_vs0, rects_vs0_length);
	GLuint fs = gl_compile_binary(GL_FRAGMENT_SHADER, rects_fs0, rects_fs0_length);
	if (!vs || !fs) return 0;
	GLuint program = glCreateProgram();
	glAttachShader(program, vs);
	glAttachShader(program, fs);
	glLinkProgram(program);
	glDeleteShader(vs);
	glDeleteShader(fs);
	GLint ok = GL_FALSE;
	glGetProgramiv(program, GL_LINK_STATUS, &ok);
	if (ok == GL_FALSE) {
		glDeleteProgram(program);
		return 0;
	}
	return program;
}

static bool same_color(SDL_Color a, SDL_Color b)
{
	return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static void SDL_RenderFlush(SDL_Renderer *r)
{
	if (r->batch_vertices.empty()) return;
	SDL_Color c = r->batch_color;
	glUseProgram(r->program);
	glEnable(GL_BLEND);
	glBlendEquation(GL_FUNC_ADD);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glUniform4f(r->color_loc, c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f);
	glVertexAttribPointer(r->pos_loc, 2, GL_FLOAT, GL_FALSE, 0, r->batch_vertices.data());
	glEnableVertexAttribArray(r->pos_loc);
	glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(r->batch_vertices.size() / 2));
	r->batch_vertices.clear();
}

static void SDL_SetRenderDrawColor(SDL_Renderer *r, uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha)
{
	SDL_Color next = (SDL_Color){red, green, blue, alpha};
	if (!r->batch_vertices.empty() && !same_color(r->batch_color, next)) SDL_RenderFlush(r);
	r->draw_color = next;
	r->batch_color = next;
}

static void SDL_RenderClear(SDL_Renderer *r)
{
	SDL_RenderFlush(r);
	SDL_Color c = r->draw_color;
	glClearColor(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

static void SDL_RenderFillRect(SDL_Renderer *r, const SDL_Rect *rc)
{
	float x1 = (rc->x / 1920.0f) * 2.0f - 1.0f;
	float x2 = ((rc->x + rc->w) / 1920.0f) * 2.0f - 1.0f;
	float y1 = 1.0f - (rc->y / 1080.0f) * 2.0f;
	float y2 = 1.0f - ((rc->y + rc->h) / 1080.0f) * 2.0f;
	const GLfloat vertices[] = { x1, y1, x2, y1, x1, y2, x2, y1, x2, y2, x1, y2 };
	r->batch_vertices.insert(r->batch_vertices.end(), vertices, vertices + 12);
	if (r->batch_vertices.size() >= 24000) SDL_RenderFlush(r);
}

static void SDL_RenderPresent(SDL_Renderer *r)
{
	SDL_RenderFlush(r);
	eglSwapBuffers(r->display, r->surface);
}

static void SDL_DestroyRenderer(SDL_Renderer *r)
{
	if (!r) return;
	SDL_RenderFlush(r);
	if (r->program) glDeleteProgram(r->program);
	if (r->display != EGL_NO_DISPLAY) {
		eglMakeCurrent(r->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		if (r->surface != EGL_NO_SURFACE) eglDestroySurface(r->display, r->surface);
		if (r->context != EGL_NO_CONTEXT) eglDestroyContext(r->display, r->context);
		eglTerminate(r->display);
	}
	delete r;
}

static void SDL_DestroyWindow(SDL_Window *w)
{
	delete w;
}

static void SDL_Quit() {}

static void color(SDL_Renderer *r, SDL_Color c)
{
	SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}

static void fill_rect(SDL_Renderer *r, int x, int y, int w, int h, SDL_Color c)
{
	SDL_Rect rc = { x, y, w, h };
	color(r, c);
	SDL_RenderFillRect(r, &rc);
}

static void draw_text(SDL_Renderer *r, int x, int y, int scale, const char *text, SDL_Color c)
{
	color(r, c);
	for (const char *p = text; *p; p++) {
		if (*p == '\n') break;
		for (int row = 0; row < 7; row++) {
			uint8_t bits = glyph_row(*p, row);
			for (int col = 0; col < 5; col++) {
				if (bits & (1 << (4 - col))) {
					SDL_Rect px = { x + col * scale, y + row * scale, scale, scale };
					SDL_RenderFillRect(r, &px);
				}
			}
		}
		x += 6 * scale;
	}
}

static void draw_text_block(SDL_Renderer *r, int x, int y, int max_w, int max_lines,
                            int scale, const std::string &text, SDL_Color c)
{
	int max_chars = max_w / (6 * scale);
	if (max_chars < 1) return;
	std::string line;
	int lines = 0;
	for (size_t i = 0; i <= text.size() && lines < max_lines; i++) {
		char ch = i < text.size() ? text[i] : '\n';
		if (ch == '\r') continue;
		if (ch == '\n' || (int)line.size() >= max_chars) {
			draw_text(r, x, y + lines * 9 * scale, scale, line.c_str(), c);
			line.clear();
			lines++;
			if (ch != '\n' && i < text.size()) line += ch;
		} else {
			line += ch;
		}
	}
}

static std::vector<std::string> ui_model_items()
{
	std::vector<std::string> items;
	if (g_model_loaded) items.push_back("[UNLOAD CURRENT MODEL]");
	std::vector<std::string> models = list_models();
	for (size_t i = 0; i < models.size(); i++) items.push_back(models[i]);
	return items;
}

static void refresh_ui_models(std::vector<std::string> &items, int *selected)
{
	items = ui_model_items();
	if (*selected < 0) *selected = 0;
	if (!items.empty() && *selected >= (int)items.size()) *selected = (int)items.size() - 1;
	if (items.empty()) *selected = 0;
}

static void refresh_ui_logs(std::string &logs_view)
{
	logs_view = log_tail(1800);
}

static void render_sidebar(SDL_Renderer *r, int screen)
{
	static const char *names[] = {"STATUS", "MODELS", "LOGS", "CONFIG", "EXIT"};
	SDL_Color text = {238, 232, 255, 255};
	SDL_Color dim = {156, 132, 195, 255};
	SDL_Color sel = {126, 58, 242, 255};
	fill_rect(r, 0, 0, 300, 1080, (SDL_Color){9, 6, 16, 255});
	draw_text(r, 34, 36, 6, "AETHER", (SDL_Color){196, 132, 255, 255});
	draw_text(r, 38, 102, 3, "PS4 LLM SERVER", dim);
	for (int i = 0; i < UI_COUNT; i++) {
		int y = 190 + i * 82;
		if (i == screen) fill_rect(r, 24, y - 18, 246, 58, sel);
		draw_text(r, 52, y, 4, names[i], i == screen ? (SDL_Color){10, 5, 18, 255} : text);
	}
}

static void render_panel_title(SDL_Renderer *r, const char *title, const char *hint)
{
	draw_text(r, 346, 68, 6, title, (SDL_Color){246, 241, 255, 255});
	draw_text(r, 350, 130, 3, hint, (SDL_Color){174, 151, 214, 255});
}

static void render_focus_badge(SDL_Renderer *r, UiFocus focus)
{
	SDL_Color bg = focus == FOCUS_TAB ? (SDL_Color){177, 94, 255, 255} : (SDL_Color){48, 28, 74, 255};
	SDL_Color fg = focus == FOCUS_TAB ? (SDL_Color){10, 5, 18, 255} : (SDL_Color){238, 232, 255, 255};
	fill_rect(r, 1660, 72, 160, 42, bg);
	draw_text(r, 1692, 84, 3, focus == FOCUS_TAB ? "TAB" : "MENU", fg);
}

static void render_ui(SDL_Renderer *r, int screen, UiFocus focus, int model_sel, int config_field,
                      const std::vector<std::string> &model_items, bool model_cache_ready,
                      const std::string &logs_view, const std::string &notice)
{
	SDL_Color bg = {4, 2, 8, 255};
	SDL_Color panel = {20, 12, 31, 255};
	SDL_Color panel2 = {38, 22, 58, 255};
	SDL_Color text = {238, 232, 255, 255};
	SDL_Color dim = {174, 151, 214, 255};
	SDL_Color accent = {177, 94, 255, 255};
	SDL_Color warn = {255, 176, 82, 255};

	color(r, bg);
	SDL_RenderClear(r);
	render_sidebar(r, screen);
	render_focus_badge(r, focus);
	fill_rect(r, 320, 172, 1560, 790, panel);

	char line[512];
	if (screen == UI_STATUS) {
		render_panel_title(r, "STATUS", "LAN API AND RUNTIME STATE");
		int mt; float tp; get_config(&mt, &tp);
		int api_type = get_api_type();
		snprintf(line, sizeof(line), "HTTP  http://%s:%d/", g_ip, LLM_PORT);
		draw_text(r, 370, 230, 4, line, text);
		snprintf(line, sizeof(line), "API   http://%s:%d/v1", g_ip, LLM_PORT);
		draw_text(r, 370, 286, 4, line, text);
		snprintf(line, sizeof(line), "SERVER   %s", g_server_listening ? "LISTENING" : "NOT LISTENING");
		draw_text(r, 370, 370, 4, line, g_server_listening ? accent : warn);
		snprintf(line, sizeof(line), "MODEL    %s%s",
		         g_model_loading ? "LOADING " : "",
		         g_model_loaded ? g_loaded_name : "NONE");
		draw_text_block(r, 370, 426, 1380, 3, 4, line, text);
		snprintf(line, sizeof(line), "DEFAULTS %d TOKENS  TEMP %.1f  API %s", mt, tp, api_type_name(api_type));
		draw_text(r, 370, 552, 4, line, text);
		snprintf(line, sizeof(line), "REQUESTS %llu", (unsigned long long)g_requests);
		draw_text(r, 370, 608, 4, line, text);
		snprintf(line, sizeof(line), "JB 0x%08x  VIDEO 0x%08x  PIGLET 0x%08x",
		         g_jb_call, g_dbg_mod_video, g_dbg_mod_piglet);
		draw_text(r, 370, 706, 3, line, dim);
		snprintf(line, sizeof(line), "PAD h=0x%08x read=%d conn=%d btn=0x%08x last=0x%08x",
		         g_dbg_pad_handle, g_dbg_pad_read, g_dbg_pad_connected,
		         g_dbg_pad_buttons, g_dbg_last_button);
		draw_text(r, 370, 756, 3, line, dim);
		snprintf(line, sizeof(line), "UI screen=%d focus=%s action=%d fps=%u frame=%ums",
		         g_dbg_ui_screen, g_dbg_ui_focus ? "tab" : "menu", g_dbg_ui_action,
		         g_dbg_fps, g_dbg_frame_ms);
		draw_text(r, 370, 806, 3, line, dim);
		ps4_gnm_stats gs;
		ps4_gnm_get_stats(&gs);
		snprintf(line, sizeof(line), "GPU ready=%d test=%d hits=%llu fb=%llu cache=%lluMB last=%dx%dx%d %lluus",
		         gs.ready, gs.selftest_ok, (unsigned long long)gs.hits,
		         (unsigned long long)gs.fallbacks,
		         (unsigned long long)(gs.cache_bytes >> 20),
		         gs.last_m, gs.last_n, gs.last_k,
		         (unsigned long long)gs.last_us);
		draw_text(r, 370, 856, 3, line, dim);
	} else if (screen == UI_MODELS) {
		render_panel_title(r, "MODELS", "LOAD OR UNLOAD GGUF FILES FROM /USER/DATA/LLM_MODELS");
		snprintf(line, sizeof(line), "CURRENT  %s", g_model_loaded ? g_loaded_name : "NONE");
		draw_text_block(r, 370, 220, 1380, 2, 4, line, text);
		if (!model_cache_ready) {
			draw_text(r, 370, 350, 4, "PRESS CROSS TO REFRESH MODEL LIST", accent);
			draw_text(r, 370, 410, 3, MODEL_DIR, dim);
		} else if (model_items.empty()) {
			draw_text(r, 370, 350, 4, "NO .GGUF FILES FOUND", warn);
			draw_text(r, 370, 410, 3, MODEL_DIR, dim);
		} else {
			if (model_sel < 0) model_sel = 0;
			if (model_sel >= (int)model_items.size()) model_sel = (int)model_items.size() - 1;
			int start = model_sel > 12 ? model_sel - 12 : 0;
			for (int row = 0; row < 15 && start + row < (int)model_items.size(); row++) {
				int idx = start + row;
				int y = 340 + row * 42;
				if (idx == model_sel) fill_rect(r, 360, y - 12, 1450, 38, focus == FOCUS_TAB ? accent : panel2);
				draw_text_block(r, 382, y, 1380, 1, 3, model_items[idx],
				                idx == model_sel && focus == FOCUS_TAB ? (SDL_Color){10, 5, 18, 255} : text);
			}
		}
	} else if (screen == UI_LOGS) {
		render_panel_title(r, "LOGS", "RECENT SERVER AND LLAMA OUTPUT");
		fill_rect(r, 370, 220, 430, 52, focus == FOCUS_TAB ? accent : panel2);
		draw_text(r, 396, 236, 3, "REFRESH LOGS", focus == FOCUS_TAB ? (SDL_Color){10, 5, 18, 255} : text);
		draw_text_block(r, 370, 310, 1420, 19, 3, logs_view, text);
	} else if (screen == UI_CONFIG) {
		render_panel_title(r, "CONFIG", "DEFAULTS USED WHEN API REQUESTS OMIT OPTIONS");
		int mt; float tp; get_config(&mt, &tp);
		int api_type = get_api_type();
		const char *labels[] = {"MAX_TOKENS", "TEMPERATURE", "API_TYPE"};
		snprintf(line, sizeof(line), "%s  %d", labels[0], mt);
		if (config_field == 0) fill_rect(r, 360, 250, 740, 60, accent);
		draw_text(r, 390, 268, 4, line, config_field == 0 ? (SDL_Color){10, 5, 18, 255} : text);
		snprintf(line, sizeof(line), "%s  %.1f", labels[1], tp);
		if (config_field == 1) fill_rect(r, 360, 340, 740, 60, accent);
		draw_text(r, 390, 358, 4, line, config_field == 1 ? (SDL_Color){10, 5, 18, 255} : text);
		snprintf(line, sizeof(line), "%s  %s", labels[2], api_type_name(api_type));
		if (config_field == 2) fill_rect(r, 360, 430, 740, 60, accent);
		draw_text(r, 390, 448, 4, line, config_field == 2 ? (SDL_Color){10, 5, 18, 255} : text);
		draw_text(r, 370, 560, 3, "UP/DOWN MENU   LEFT/RIGHT FIELD   CROSS CHANGES VALUE", dim);
	} else {
		render_panel_title(r, "EXIT", "STOP THE SERVER AND RETURN TO THE PS4 SHELL");
		draw_text(r, 370, 270, 5, "PRESS CROSS TO EXIT", warn);
		draw_text(r, 370, 350, 4, "PRESS CIRCLE TO RETURN", text);
	}

	if (!notice.empty()) {
		fill_rect(r, 330, 980, 1540, 54, panel2);
		draw_text_block(r, 352, 996, 1440, 1, 3, notice, accent);
	} else {
		draw_text(r, 340, 1000, 3,
		          focus == FOCUS_TAB ? "TAB: UP/DOWN SELECT   CROSS ACTION   CIRCLE MENU   OPTIONS EXIT"
		                             : "MENU: UP/DOWN TAB   CROSS ENTER TAB   OPTIONS EXIT", dim);
	}
}

static UiAction map_pad_edge(unsigned edge)
{
	if (edge & ORBIS_PAD_BUTTON_OPTIONS)  { g_dbg_last_button = ORBIS_PAD_BUTTON_OPTIONS;  return ACT_QUIT; }
	if (edge & ORBIS_PAD_BUTTON_UP)       { g_dbg_last_button = ORBIS_PAD_BUTTON_UP;       return ACT_UP; }
	if (edge & ORBIS_PAD_BUTTON_DOWN)     { g_dbg_last_button = ORBIS_PAD_BUTTON_DOWN;     return ACT_DOWN; }
	if (edge & ORBIS_PAD_BUTTON_LEFT)     { g_dbg_last_button = ORBIS_PAD_BUTTON_LEFT;     return ACT_LEFT; }
	if (edge & ORBIS_PAD_BUTTON_RIGHT)    { g_dbg_last_button = ORBIS_PAD_BUTTON_RIGHT;    return ACT_RIGHT; }
	if (edge & ORBIS_PAD_BUTTON_CROSS)    { g_dbg_last_button = ORBIS_PAD_BUTTON_CROSS;    return ACT_SELECT; }
	if (edge & ORBIS_PAD_BUTTON_CIRCLE)   { g_dbg_last_button = ORBIS_PAD_BUTTON_CIRCLE;   return ACT_BACK; }
	return ACT_NONE;
}

static UiAction read_ui_action()
{
	static unsigned repeat_buttons = 0;
	static Uint32 next_repeat = 0;
	const unsigned repeat_mask = ORBIS_PAD_BUTTON_UP | ORBIS_PAD_BUTTON_DOWN |
	                             ORBIS_PAD_BUTTON_LEFT | ORBIS_PAD_BUTTON_RIGHT;

	if (g_pad_handle >= 0) {
		OrbisPadData data[8]; memset(data, 0, sizeof(data));
		int rc = scePadRead(g_pad_handle, data, 8);
		g_dbg_pad_read = rc;
		if (rc <= 0) {
			rc = scePadReadState(g_pad_handle, &data[0]);
			g_dbg_pad_read = rc;
			if (rc == 0) rc = 1;
		}
		if (rc > 8) rc = 8;

		unsigned latest = 0;
		bool connected = false;
		for (int i = 0; i < rc; i++) {
			g_dbg_pad_connected = data[i].connected;
			g_dbg_pad_buttons = data[i].buttons;
			if (!data[i].connected) continue;
			connected = true;
			latest = data[i].buttons;
			unsigned edge = data[i].buttons & ~g_pad_prev;
			g_pad_prev = data[i].buttons;
			UiAction a = map_pad_edge(edge);
			if (a != ACT_NONE) {
				repeat_buttons = data[i].buttons & repeat_mask;
				next_repeat = SDL_GetTicks() + 140;
				return a;
			}
		}

		if (connected) {
			unsigned held = latest & repeat_mask;
			if (held) {
				Uint32 now = SDL_GetTicks();
				if (held != repeat_buttons) {
					repeat_buttons = held;
					next_repeat = now + 140;
				} else if (now >= next_repeat) {
					next_repeat = now + 70;
					UiAction a = map_pad_edge(held);
					if (a != ACT_NONE) return a;
				}
			} else {
				repeat_buttons = 0;
				next_repeat = 0;
			}
		} else {
			g_dbg_pad_connected = 0;
		}
	}
	return ACT_NONE;
}

static bool init_sdl_ui(SDL_Window **win, SDL_Renderer **ren)
{
	load_visual_modules();
	init_direct_pad();
	g_dbg_sdl_init = -1;
	g_dbg_img_ok = 0;
	g_dbg_img_w = 1920;
	g_dbg_img_h = 1080;

	OrbisPglConfig pgl_config;
	memset(&pgl_config, 0, sizeof(pgl_config));
	pgl_config.size = sizeof(pgl_config);
	pgl_config.flags = ORBIS_PGL_FLAGS_USE_COMPOSITE_EXT |
	                   ORBIS_PGL_FLAGS_USE_FLEXIBLE_MEMORY | 0x60;
	pgl_config.processOrder = 1;
	pgl_config.systemSharedMemorySize = 0x200000;
	pgl_config.videoSharedMemorySize = 0x2400000;
	pgl_config.maxMappedFlexibleMemory = 0xAA00000;
	pgl_config.drawCommandBufferSize = 0xC0000;
	pgl_config.lcueResourceBufferSize = 0x10000;
	pgl_config.dbgPosCmd_0x40 = 1920;
	pgl_config.dbgPosCmd_0x44 = 1080;
	pgl_config.unk_0x5C = 2;

	if (!scePigletSetConfigurationVSH(&pgl_config)) {
		set_ui_error("scePigletSetConfigurationVSH failed flags=0x%08x sys=%llu video=%llu flex=%llu",
		             pgl_config.flags,
		             (unsigned long long)pgl_config.systemSharedMemorySize,
		             (unsigned long long)pgl_config.videoSharedMemorySize,
	             (unsigned long long)pgl_config.maxMappedFlexibleMemory);
		return false;
	}
	logln("Piglet config Store/liborbis flags=0x%08x sys=0x%llx video=0x%llx flex=0x%llx",
	      pgl_config.flags,
	      (unsigned long long)pgl_config.systemSharedMemorySize,
	      (unsigned long long)pgl_config.videoSharedMemorySize,
	      (unsigned long long)pgl_config.maxMappedFlexibleMemory);

	EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (display == EGL_NO_DISPLAY) {
		set_ui_error("eglGetDisplay failed");
		return false;
	}
	EGLint major = 0, minor = 0;
	if (!eglInitialize(display, &major, &minor)) {
		set_ui_error("eglInitialize failed: 0x%08x", eglGetError());
		return false;
	}
	if (!eglBindAPI(EGL_OPENGL_ES_API)) {
		set_ui_error("eglBindAPI failed: 0x%08x", eglGetError());
		eglTerminate(display);
		return false;
	}
	eglSwapInterval(display, 0);

	const EGLint attribs[] = {
		EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
		EGL_DEPTH_SIZE, 16, EGL_STENCIL_SIZE, 0,
		EGL_SAMPLE_BUFFERS, 0, EGL_SAMPLES, 0,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_NONE,
	};
	EGLConfig config;
	EGLint num_configs = 0;
	if (!eglChooseConfig(display, attribs, &config, 1, &num_configs) || num_configs < 1) {
		set_ui_error("eglChooseConfig failed: 0x%08x configs=%d", eglGetError(), num_configs);
		eglTerminate(display);
		return false;
	}

	OrbisPglWindow render_window = {0, 1920, 1080, 0};
	const EGLint window_attribs[] = { EGL_RENDER_BUFFER, EGL_BACK_BUFFER, EGL_NONE };
	EGLSurface surface = eglCreateWindowSurface(display, config, &render_window, window_attribs);
	if (surface == EGL_NO_SURFACE) {
		set_ui_error("eglCreateWindowSurface failed: 0x%08x", eglGetError());
		eglTerminate(display);
		return false;
	}
	const EGLint ctx_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, ctx_attribs);
	if (context == EGL_NO_CONTEXT) {
		set_ui_error("eglCreateContext failed: 0x%08x", eglGetError());
		eglDestroySurface(display, surface);
		eglTerminate(display);
		return false;
	}
	if (!eglMakeCurrent(display, surface, surface, context)) {
		set_ui_error("eglMakeCurrent failed: 0x%08x", eglGetError());
		eglDestroyContext(display, context);
		eglDestroySurface(display, surface);
		eglTerminate(display);
		return false;
	}

	GLuint program = gl_build_rect_program();
	if (!program) {
		set_ui_error("rect shader init failed: gl=0x%08x", glGetError());
		eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		eglDestroyContext(display, context);
		eglDestroySurface(display, surface);
		eglTerminate(display);
		return false;
	}

	*win = new SDL_Window();
	*ren = new SDL_Renderer();
	(*ren)->display = display;
	(*ren)->surface = surface;
	(*ren)->context = context;
	(*ren)->program = program;
	(*ren)->pos_loc = glGetAttribLocation(program, "a_Position");
	(*ren)->color_loc = glGetUniformLocation(program, "u_color");
	(*ren)->draw_color = (SDL_Color){0, 0, 0, 255};
	(*ren)->batch_color = (*ren)->draw_color;
	(*ren)->batch_vertices.reserve(65536);
	glViewport(0, 0, 1920, 1080);
	g_dbg_sdl_init = 0;
	g_dbg_win_ok = 1;
	g_dbg_ren_ok = 1;
	logln("Piglet GLES UI ready EGL %d.%d", major, minor);
	return true;
}

static bool run_sdl_ui()
{
	SDL_Window *win = NULL;
	SDL_Renderer *ren = NULL;
	if (!init_sdl_ui(&win, &ren)) {
		if (ren) SDL_DestroyRenderer(ren);
		if (win) SDL_DestroyWindow(win);
		SDL_Quit();
		return false;
	}

	int screen = UI_STATUS;
	int last_screen = -1;
	int model_sel = 0;
	int config_field = 0;
	UiFocus focus = FOCUS_MENU;
	std::vector<std::string> model_items;
	bool model_cache_ready = false;
	std::string logs_view = "PRESS CROSS TO REFRESH LOGS.";
	std::string notice = "NATIVE PIGLET UI READY";
	Uint32 notice_until = SDL_GetTicks() + 2500;
	bool running = true;
	bool was_loading = false;
	Uint32 fps_t0 = SDL_GetTicks();
	unsigned fps_frames = 0;

	while (running) {
		if (screen != last_screen) {
			focus = FOCUS_MENU;
			last_screen = screen;
		}

		UiAction a = read_ui_action();
		g_dbg_ui_action = a;
		if (a == ACT_QUIT) {
			running = false;
		} else if (a == ACT_BACK) {
			if (focus == FOCUS_TAB) focus = FOCUS_MENU;
			else screen = UI_STATUS;
		} else if (a == ACT_LEFT) {
			if (screen == UI_CONFIG) config_field = (config_field + CONFIG_FIELD_COUNT - 1) % CONFIG_FIELD_COUNT;
			else if (focus == FOCUS_MENU) screen = (screen + UI_COUNT - 1) % UI_COUNT;
		} else if (a == ACT_RIGHT) {
			if (screen == UI_CONFIG) config_field = (config_field + 1) % CONFIG_FIELD_COUNT;
			else if (focus == FOCUS_MENU) screen = (screen + 1) % UI_COUNT;
		} else if (a == ACT_UP) {
			if (focus == FOCUS_TAB) {
				if (screen == UI_MODELS && !model_items.empty())
					model_sel = (model_sel + (int)model_items.size() - 1) % (int)model_items.size();
			} else {
				screen = (screen + UI_COUNT - 1) % UI_COUNT;
			}
		} else if (a == ACT_DOWN) {
			if (focus == FOCUS_TAB) {
				if (screen == UI_MODELS && !model_items.empty())
					model_sel = (model_sel + 1) % (int)model_items.size();
			} else {
				screen = (screen + 1) % UI_COUNT;
			}
		} else if (a == ACT_SELECT) {
			if (screen == UI_STATUS) {
				screen = UI_MODELS;
			} else if (screen == UI_CONFIG) {
				cycle_config_value(config_field, 1);
			} else if (screen == UI_EXIT) {
				running = false;
			} else if (screen == UI_LOGS) {
				focus = FOCUS_TAB;
				refresh_ui_logs(logs_view);
				notice = "LOGS REFRESHED";
				notice_until = SDL_GetTicks() + 1500;
			} else if (screen == UI_MODELS) {
				if (focus != FOCUS_TAB) {
					focus = FOCUS_TAB;
					refresh_ui_models(model_items, &model_sel);
					model_cache_ready = true;
				} else if (!model_items.empty()) {
					if (model_sel >= (int)model_items.size()) model_sel = (int)model_items.size() - 1;
					std::string item = model_items[model_sel];
					if (g_model_loaded && model_sel == 0) {
						if (pthread_mutex_trylock(&g_model_lock) == 0) {
							unload_model_locked();
							pthread_mutex_unlock(&g_model_lock);
							refresh_ui_models(model_items, &model_sel);
							model_cache_ready = true;
							notice = "MODEL UNLOADED";
							notice_until = SDL_GetTicks() + 2500;
							logln("model unloaded from SDL UI");
						} else {
							notice = "MODEL BUSY";
							notice_until = SDL_GetTicks() + 2500;
						}
					} else {
						if (start_ui_model_load(item)) {
							notice = "LOADING " + item;
							notice_until = SDL_GetTicks() + 60000;
						} else {
							notice = "MODEL LOAD ALREADY RUNNING";
							notice_until = SDL_GetTicks() + 2500;
						}
					}
				}
			}
		}

		if (screen != last_screen) {
			focus = FOCUS_MENU;
			last_screen = screen;
		}
		g_dbg_ui_screen = screen;
		g_dbg_ui_focus = focus == FOCUS_TAB ? 1 : 0;

		if (was_loading && !g_model_loading) {
			notice = g_model_loaded ? "MODEL LOADED" : "MODEL LOAD FAILED";
			notice_until = SDL_GetTicks() + 3500;
			if (screen == UI_MODELS) {
				refresh_ui_models(model_items, &model_sel);
				model_cache_ready = true;
			}
		}
		was_loading = g_model_loading;
		std::string live_notice = g_model_loading ? notice :
			(SDL_GetTicks() < notice_until ? notice : "");
		Uint32 frame_start = SDL_GetTicks();
		render_ui(ren, screen, focus, model_sel, config_field, model_items, model_cache_ready, logs_view, live_notice);
		SDL_RenderPresent(ren);
		g_dbg_frame_ms = SDL_GetTicks() - frame_start;
		SDL_Delay(16);
		g_dbg_frames++;
		fps_frames++;
		Uint32 fps_now = SDL_GetTicks();
		if (fps_now - fps_t0 >= 1000) {
			g_dbg_fps = (unsigned)((fps_frames * 1000u) / (fps_now - fps_t0));
			fps_frames = 0;
			fps_t0 = fps_now;
		}
	}

	if (g_pad_handle >= 0) scePadClose(g_pad_handle);
	SDL_DestroyRenderer(ren);
	SDL_DestroyWindow(win);
	SDL_Quit();
	return true;
}
void run_ps4_ui()
{
	if (run_sdl_ui()) return;
	char msg[512];
	snprintf(msg, sizeof(msg),
		"Aether Piglet UI failed.\n\n%s\n\nFalling back to dialog UI.", g_dbg_err);
	show_ok(msg);
	run_dialog_ui();
}

