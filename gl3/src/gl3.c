#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <math.h>
#include <setjmp.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
/* jpeglib.h needs stdio.h's FILE and stddef.h's size_t already defined */
#include <jpeglib.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

/* gl3 config, super is the modifier key, layout is dwm-style master/stack */
#define GL3_NUM_WORKSPACES 10 /* super+1-0 switches, super+shift+1-0 moves */
/* default wallpaper, DATADIR is set by the makefile at build time */
#define GL3_BG_IMAGE GL3_DATADIR "/background.jpg"
/* drm's xrgb8888 fourcc code, matches drm_fourcc.h's DRM_FORMAT_XRGB8888 */
#define GL3_DRM_FORMAT_XRGB8888 0x34325258

/* everything below is user-tweakable, see load_config() and gl3.conf.example
 * these are just the fallback values if no config file exists */
struct gl3_config {
	char terminal[256];
	char launcher[512]; /* super+space, an app launcher command */
	int gap;
	int master_ratio; /* 1-9, tenths of the screen width */
	int border_width;
	float border_active[4];
	float border_inactive[4];
	float inactive_opacity;
	float bg_color[4]; /* used if the wallpaper fails to load */
	char wallpaper[512]; /* empty means use the built-in default */

	/* every keybind below is held with super, keys are matched case-
	 * insensitively (xkb_keysym_to_lower on both sides) since shift
	 * changes which keysym a keycode produces on most layouts. super+1-0
	 * and super+shift+1-0 (workspaces) aren't configurable, they're tied
	 * to the number row by convention like every other tiling wm */
	xkb_keysym_t key_terminal;
	xkb_keysym_t key_launcher;
	xkb_keysym_t key_help;
	xkb_keysym_t key_close;
	xkb_keysym_t key_focus_next;
	xkb_keysym_t key_shrink_master;
	xkb_keysym_t key_grow_master;
	xkb_keysym_t key_swap_master;
	xkb_keysym_t key_quit;
};

static struct gl3_config config = {
	.terminal = "foot",
	/* fuzzel is a real wlr-layer-shell popup (see the layer-shell code
	 * below), not a terminal - it lists both PATH binaries and desktop
	 * entries, and execs the selection itself, no wrapper script needed */
	.launcher = "fuzzel",
	.gap = 10,
	.master_ratio = 6,
	.border_width = 3,
	.border_active = { 0.92f, 0.22f, 0.26f, 1.0f }, /* gurren lagann crimson */
	.border_inactive = { 0.19f, 0.16f, 0.20f, 1.0f }, /* muted plum-gray */
	.inactive_opacity = 0.75f,
	.bg_color = { 0.16f, 0.02f, 0.03f, 1.0f }, /* deep gurren crimson */
	.wallpaper = "",
	.key_terminal = XKB_KEY_Return,
	.key_launcher = XKB_KEY_space,
	.key_help = XKB_KEY_question,
	.key_close = XKB_KEY_q,
	.key_focus_next = XKB_KEY_j,
	.key_shrink_master = XKB_KEY_h,
	.key_grow_master = XKB_KEY_l,
	.key_swap_master = XKB_KEY_m,
	.key_quit = XKB_KEY_Escape,
};

/* keybind list shown by super+?, and on first launch, see write_help_file -
 * generated at runtime since every entry except workspace switching is
 * configurable (see load_config()), a static list would go stale the
 * moment someone rebinds a key */
struct gl3_help_entry {
	const char *label;
	const char *conf_key; /* the gl3.conf key name, eg "keybind_close" */
	xkb_keysym_t *key;
};

static const struct gl3_help_entry GL3_HELP_ENTRIES[] = {
	{ "terminal", "keybind_terminal", &config.key_terminal },
	{ "launcher", "keybind_launcher", &config.key_launcher },
	{ "close window", "keybind_close", &config.key_close },
	{ "focus next", "keybind_focus_next", &config.key_focus_next },
	{ "swap with master", "keybind_swap_master", &config.key_swap_master },
	{ "shrink master", "keybind_shrink_master", &config.key_shrink_master },
	{ "grow master", "keybind_grow_master", &config.key_grow_master },
	{ "this help", "keybind_help", &config.key_help },
	{ "quit", "keybind_quit", &config.key_quit },
};
#define GL3_HELP_NUM_ENTRIES (sizeof(GL3_HELP_ENTRIES) / sizeof(GL3_HELP_ENTRIES[0]))
/* filled in by write_help_file() in main(), read by the super+? keybind */
static char gl3_help_path[1024] = "";

/* parses "rrggbb" or "rrggbbaa" hex into 0-1 floats, false on bad input */
static bool parse_hex_color(const char *s, float out[4]) {
	size_t len = strlen(s);
	if (len != 6 && len != 8) {
		return false;
	}
	unsigned int r, g, b, a = 255;
	int n = sscanf(s, len == 8 ? "%2x%2x%2x%2x" : "%2x%2x%2x", &r, &g, &b, &a);
	if (n != (len == 8 ? 4 : 3)) {
		return false;
	}
	out[0] = r / 255.0f;
	out[1] = g / 255.0f;
	out[2] = b / 255.0f;
	out[3] = a / 255.0f;
	return true;
}

/* parses a key name (eg "return", "space", "q") into a keysym, false if
 * xkbcommon doesn't recognize it - accepts anything xkb_keysym_get_name()
 * would print, matched case-insensitively */
static bool parse_keybind(const char *s, xkb_keysym_t *out) {
	xkb_keysym_t sym = xkb_keysym_from_name(s, XKB_KEYSYM_CASE_INSENSITIVE);
	if (sym == XKB_KEY_NoSymbol) {
		return false;
	}
	*out = sym;
	return true;
}

/* strips leading and trailing whitespace in place, returns the new start */
static char *trim(char *s) {
	while (isspace((unsigned char)*s)) {
		s++;
	}
	if (*s == '\0') {
		return s;
	}
	char *end = s + strlen(s) - 1;
	while (end > s && isspace((unsigned char)*end)) {
		*end = '\0';
		end--;
	}
	return s;
}

/* like `mkdir -p`, creates every missing directory component in path, logs
 * a warning if the final component still doesn't exist afterward */
static void mkdir_p(const char *path) {
	char buf[1024];
	snprintf(buf, sizeof(buf), "%s", path);
	for (char *p = buf + 1; *p != '\0'; p++) {
		if (*p == '/') {
			*p = '\0';
			mkdir(buf, 0755);
			*p = '/';
		}
	}
	if (mkdir(buf, 0755) != 0 && errno != EEXIST) {
		wlr_log(WLR_ERROR, "mkdir_p: couldn't create %s (%s)", buf, strerror(errno));
	}
}

/* reads ~/.config/gl3/gl3.conf if it exists, see gl3.conf.example for the
 * format, if the file is missing the defaults above are used as-is */
static void load_config(void) {
	const char *xdg_config = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	char path[1024];
	if (xdg_config != NULL && xdg_config[0] != '\0') {
		snprintf(path, sizeof(path), "%s/gl3/gl3.conf", xdg_config);
	} else if (home != NULL) {
		snprintf(path, sizeof(path), "%s/.config/gl3/gl3.conf", home);
	} else {
		return;
	}

	FILE *f = fopen(path, "r");
	if (f == NULL) {
		return;
	}

	char line[512];
	int lineno = 0;
	while (fgets(line, sizeof(line), f) != NULL) {
		lineno++;
		char *trimmed = trim(line);
		if (trimmed[0] == '\0' || trimmed[0] == '#') {
			continue;
		}
		char *eq = strchr(trimmed, '=');
		if (eq == NULL) {
			wlr_log(WLR_ERROR, "gl3.conf:%d: missing '=', skipping line", lineno);
			continue;
		}
		*eq = '\0';
		char *key = trim(trimmed);
		char *value = trim(eq + 1);

		if (strcmp(key, "terminal") == 0) {
			snprintf(config.terminal, sizeof(config.terminal), "%s", value);
		} else if (strcmp(key, "launcher") == 0) {
			snprintf(config.launcher, sizeof(config.launcher), "%s", value);
		} else if (strcmp(key, "gap") == 0) {
			int v = atoi(value);
			if (v >= 0) {
				config.gap = v;
			} else {
				wlr_log(WLR_ERROR, "gl3.conf:%d: gap must be >= 0", lineno);
			}
		} else if (strcmp(key, "master_ratio") == 0) {
			int v = atoi(value);
			if (v >= 1 && v <= 9) {
				config.master_ratio = v;
			} else {
				wlr_log(WLR_ERROR, "gl3.conf:%d: master_ratio must be 1-9", lineno);
			}
		} else if (strcmp(key, "border_width") == 0) {
			int v = atoi(value);
			if (v >= 0) {
				config.border_width = v;
			} else {
				wlr_log(WLR_ERROR, "gl3.conf:%d: border_width must be >= 0", lineno);
			}
		} else if (strcmp(key, "border_active") == 0) {
			if (!parse_hex_color(value, config.border_active)) {
				wlr_log(WLR_ERROR, "gl3.conf:%d: border_active needs rrggbb or rrggbbaa", lineno);
			}
		} else if (strcmp(key, "border_inactive") == 0) {
			if (!parse_hex_color(value, config.border_inactive)) {
				wlr_log(WLR_ERROR, "gl3.conf:%d: border_inactive needs rrggbb or rrggbbaa", lineno);
			}
		} else if (strcmp(key, "inactive_opacity") == 0) {
			float v = strtof(value, NULL);
			if (v >= 0.0f && v <= 1.0f) {
				config.inactive_opacity = v;
			} else {
				wlr_log(WLR_ERROR, "gl3.conf:%d: inactive_opacity must be 0.0-1.0", lineno);
			}
		} else if (strcmp(key, "bg_color") == 0) {
			if (!parse_hex_color(value, config.bg_color)) {
				wlr_log(WLR_ERROR, "gl3.conf:%d: bg_color needs rrggbb or rrggbbaa", lineno);
			}
		} else if (strcmp(key, "wallpaper") == 0) {
			snprintf(config.wallpaper, sizeof(config.wallpaper), "%s", value);
		} else if (strcmp(key, "keybind_terminal") == 0) {
			if (!parse_keybind(value, &config.key_terminal)) {
				wlr_log(WLR_ERROR, "gl3.conf:%d: keybind_terminal, unknown key name \"%s\"", lineno, value);
			}
		} else if (strcmp(key, "keybind_launcher") == 0) {
			if (!parse_keybind(value, &config.key_launcher)) {
				wlr_log(WLR_ERROR, "gl3.conf:%d: keybind_launcher, unknown key name \"%s\"", lineno, value);
			}
		} else if (strcmp(key, "keybind_help") == 0) {
			if (!parse_keybind(value, &config.key_help)) {
				wlr_log(WLR_ERROR, "gl3.conf:%d: keybind_help, unknown key name \"%s\"", lineno, value);
			}
		} else if (strcmp(key, "keybind_close") == 0) {
			if (!parse_keybind(value, &config.key_close)) {
				wlr_log(WLR_ERROR, "gl3.conf:%d: keybind_close, unknown key name \"%s\"", lineno, value);
			}
		} else if (strcmp(key, "keybind_focus_next") == 0) {
			if (!parse_keybind(value, &config.key_focus_next)) {
				wlr_log(WLR_ERROR, "gl3.conf:%d: keybind_focus_next, unknown key name \"%s\"", lineno, value);
			}
		} else if (strcmp(key, "keybind_shrink_master") == 0) {
			if (!parse_keybind(value, &config.key_shrink_master)) {
				wlr_log(WLR_ERROR, "gl3.conf:%d: keybind_shrink_master, unknown key name \"%s\"", lineno, value);
			}
		} else if (strcmp(key, "keybind_grow_master") == 0) {
			if (!parse_keybind(value, &config.key_grow_master)) {
				wlr_log(WLR_ERROR, "gl3.conf:%d: keybind_grow_master, unknown key name \"%s\"", lineno, value);
			}
		} else if (strcmp(key, "keybind_swap_master") == 0) {
			if (!parse_keybind(value, &config.key_swap_master)) {
				wlr_log(WLR_ERROR, "gl3.conf:%d: keybind_swap_master, unknown key name \"%s\"", lineno, value);
			}
		} else if (strcmp(key, "keybind_quit") == 0) {
			if (!parse_keybind(value, &config.key_quit)) {
				wlr_log(WLR_ERROR, "gl3.conf:%d: keybind_quit, unknown key name \"%s\"", lineno, value);
			}
		} else {
			wlr_log(WLR_ERROR, "gl3.conf:%d: unknown key \"%s\", skipping", lineno, key);
		}
	}
	fclose(f);
	wlr_log(WLR_INFO, "loaded config from %s", path);
}

/* warns if two keybind_* options ended up on the same key - the dispatch
 * order in handle_keybinding() would otherwise silently let the first
 * match shadow the rest, with no indication why the other one "stopped
 * working". runs on the defaults too (harmless, they're all distinct),
 * not just when a config file is present */
static void check_keybind_collisions(void) {
	for (size_t i = 0; i < GL3_HELP_NUM_ENTRIES; i++) {
		for (size_t j = i + 1; j < GL3_HELP_NUM_ENTRIES; j++) {
			if (xkb_keysym_to_lower(*GL3_HELP_ENTRIES[i].key) ==
					xkb_keysym_to_lower(*GL3_HELP_ENTRIES[j].key)) {
				wlr_log(WLR_ERROR,
					"gl3.conf: %s and %s are both bound to the same key, only \"%s\" will work",
					GL3_HELP_ENTRIES[i].conf_key, GL3_HELP_ENTRIES[j].conf_key,
					GL3_HELP_ENTRIES[i].label);
			}
		}
	}
}

/* struct members are commented where they are used, not here */
enum gl3_cursor_mode {
	GL3_CURSOR_PASSTHROUGH,
	GL3_CURSOR_RATIO_DRAG, /* dragging the master/stack boundary */
};

#define GL3_DRAG_THRESHOLD 6 /* px on either side of the boundary that's grabbable */

struct gl3_server {
	struct wl_display *wl_display;
	struct wlr_backend *backend;
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;
	struct wlr_scene *scene;
	struct wlr_scene_output_layout *scene_layout;

	struct wlr_xdg_shell *xdg_shell;
	struct wl_listener new_xdg_toplevel;
	struct wl_listener new_xdg_popup;

	/* tells clients to skip drawing their own title bar/decorations,
	 * gl3 already draws its own border and there's no floating mode
	 * to put a draggable title bar on anyway */
	struct wlr_xdg_decoration_manager_v1 *xdg_decoration_manager;
	struct wl_listener new_xdg_toplevel_decoration;
	struct wl_list toplevels;

	/* wlr-layer-shell-v1: bars, launchers, lock screens - anything that
	 * anchors to a screen edge instead of being tiled. layer_tree[N] are
	 * shared scene subtrees (one per protocol layer, background lowest to
	 * overlay highest), created once in main() in that order so overlay
	 * stays visually on top of the others without per-event re-raising -
	 * new toplevels still need raising above layer_tree[TOP]/[OVERLAY]
	 * though, see raise_layer_shell_overlays() */
	struct wlr_layer_shell_v1 *layer_shell;
	struct wl_listener new_layer_surface;
	struct wlr_scene_tree *layer_tree[4];

	struct wlr_cursor *cursor;
	struct wlr_xcursor_manager *cursor_mgr;
	struct wl_listener cursor_motion;
	struct wl_listener cursor_motion_absolute;
	struct wl_listener cursor_button;
	struct wl_listener cursor_axis;
	struct wl_listener cursor_frame;

	struct wlr_seat *seat;
	struct wl_listener new_input;
	struct wl_listener request_cursor;
	struct wl_listener pointer_focus_change;
	struct wl_listener request_set_selection;
	struct wl_list keyboards;
	enum gl3_cursor_mode cursor_mode;

	struct wlr_output_layout *output_layout;
	struct wl_list outputs;
	struct wl_listener new_output;

	/* wallpaper source image, shared across every output's own scaled
	 * copy - NULL means it failed to load, each output then just shows
	 * the solid-color fallback rect created in main() */
	struct wlr_buffer *bg_buffer;

	/* which output a GL3_CURSOR_RATIO_DRAG is adjusting, set when the
	 * drag starts and used for the rest of it, so the drag keeps acting
	 * on the output it started on even if the cursor strays off it */
	struct gl3_output *drag_output;

	int master_ratio;
};

struct gl3_output {
	struct wl_list link;
	struct gl3_server *server;
	struct wlr_output *wlr_output;
	struct wl_listener frame;
	struct wl_listener request_state;
	struct wl_listener destroy;
	/* this output's own copy of the wallpaper, scaled to its size, NULL
	 * if bg_buffer failed to load (falls back to the solid-color rect) */
	struct wlr_scene_buffer *bg;
	/* layer-shell surfaces anchored to this output (gl3_layer_surface.link),
	 * and what's left of this output after their exclusive zones are
	 * reserved - arrange_output() tiles into usable_area instead of the
	 * output's full box, so a bar doesn't get covered by a tiled window */
	struct wl_list layers;
	struct wlr_box usable_area;
	/* which workspace (1-GL3_NUM_WORKSPACES) is currently showing on this
	 * output - i3-style, each workspace shows on at most one output at a
	 * time, see switch_workspace() */
	int workspace;
	/* one small square per workspace, bottom-left corner, lit up in
	 * border_active for whichever workspace this output is showing right
	 * now and border_inactive for the rest - gl3 has no text rendering,
	 * so this is the on-screen "which workspace am I on" indicator */
	struct wlr_scene_rect *workspace_dots[GL3_NUM_WORKSPACES];
};

struct gl3_toplevel {
	struct wl_list link;
	struct gl3_server *server;
	struct wlr_xdg_toplevel *xdg_toplevel;
	struct wlr_scene_tree *scene_tree;
	struct wlr_scene_rect *border;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener commit;
	struct wl_listener destroy;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;
	int workspace;
};

struct gl3_popup {
	struct wlr_xdg_popup *xdg_popup;
	struct wl_listener commit;
	struct wl_listener destroy;
};

struct gl3_layer_surface {
	struct wl_list link; /* gl3_output.layers */
	struct gl3_output *output;
	struct wlr_layer_surface_v1 *layer_surface;
	struct wlr_scene_layer_surface_v1 *scene;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener destroy;
	struct wl_listener commit;
};

/* tracks one client's xdg-decoration object just long enough to force it
 * server-side (see new_xdg_toplevel_decoration) and clean up when it goes */
struct gl3_decoration {
	struct wlr_xdg_toplevel_decoration_v1 *wlr_decoration;
	struct wl_listener request_mode;
	struct wl_listener destroy;
	/* only wired up (and only meaningful to remove in the destroy
	 * handler) when the decoration object shows up before its toplevel's
	 * first commit - see server_new_xdg_toplevel_decoration() */
	struct wl_listener commit;
	bool has_commit_listener;
};

struct gl3_keyboard {
	struct wl_list link;
	struct gl3_server *server;
	struct wlr_keyboard *wlr_keyboard;

	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
};

/* defined later, near the rest of the per-output sync helpers - forward
 * declared here since focus_toplevel() and switch_workspace() both need
 * to call into them before that point in the file */
static void sync_workspace_dots(struct gl3_output *output);
static void raise_workspace_dots(struct gl3_server *server);
static void raise_layer_shell_overlays(struct gl3_server *server);

/* lets us bail out of a broken jpeg instead of libjpeg calling exit() */
struct gl3_jpeg_error {
	struct jpeg_error_mgr pub;
	jmp_buf jmp;
};

static void gl3_jpeg_error_exit(j_common_ptr cinfo) {
	struct gl3_jpeg_error *err = (struct gl3_jpeg_error *)cinfo->err;
	longjmp(err->jmp, 1);
}

/* loads a jpeg file and converts it to xrgb8888 pixels, or NULL on failure */
static uint8_t *load_jpeg_xrgb(const char *path, int *out_w, int *out_h) {
	FILE *f = fopen(path, "rb");
	if (f == NULL) {
		return NULL;
	}

	struct jpeg_decompress_struct cinfo;
	struct gl3_jpeg_error jerr;
	cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = gl3_jpeg_error_exit;

	uint8_t *pixels = NULL;
	uint8_t *row = NULL;
	if (setjmp(jerr.jmp)) {
		/* libjpeg hit an error partway through, clean up and give up */
		jpeg_destroy_decompress(&cinfo);
		free(pixels);
		free(row);
		fclose(f);
		return NULL;
	}

	jpeg_create_decompress(&cinfo);
	jpeg_stdio_src(&cinfo, f);
	jpeg_read_header(&cinfo, TRUE);
	cinfo.out_color_space = JCS_RGB;
	jpeg_start_decompress(&cinfo);

	int w = (int)cinfo.output_width;
	int h = (int)cinfo.output_height;
	pixels = malloc((size_t)w * h * 4);
	row = malloc((size_t)w * 3);

	/* libjpeg only gives us one rgb row at a time, we convert as we go */
	while (cinfo.output_scanline < cinfo.output_height) {
		int y = (int)cinfo.output_scanline;
		JSAMPROW rows[1] = { row };
		jpeg_read_scanlines(&cinfo, rows, 1);
		uint8_t *dst = pixels + (size_t)y * w * 4;
		for (int x = 0; x < w; x++) {
			/* xrgb8888 stores bytes as b, g, r, x on little-endian */
			dst[x * 4 + 0] = row[x * 3 + 2];
			dst[x * 4 + 1] = row[x * 3 + 1];
			dst[x * 4 + 2] = row[x * 3 + 0];
			dst[x * 4 + 3] = 0xff;
		}
	}
	free(row);

	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);
	fclose(f);

	*out_w = w;
	*out_h = h;
	return pixels;
}

/* wraps decoded pixels so the wlroots scene graph knows how to render them */
struct gl3_image_buffer {
	struct wlr_buffer base;
	uint8_t *pixels;
	size_t stride;
};

static void gl3_image_buffer_destroy(struct wlr_buffer *buffer) {
	struct gl3_image_buffer *buf = wl_container_of(buffer, buf, base);
	free(buf->pixels);
	free(buf);
}

static bool gl3_image_buffer_begin_data_ptr_access(struct wlr_buffer *buffer,
		uint32_t flags, void **data, uint32_t *format, size_t *stride) {
	struct gl3_image_buffer *buf = wl_container_of(buffer, buf, base);
	*data = buf->pixels;
	*format = GL3_DRM_FORMAT_XRGB8888;
	*stride = buf->stride;
	return true;
}

static void gl3_image_buffer_end_data_ptr_access(struct wlr_buffer *buffer) {
	/* pixels are plain memory, always readable, nothing to release */
}

static const struct wlr_buffer_impl gl3_image_buffer_impl = {
	.destroy = gl3_image_buffer_destroy,
	.begin_data_ptr_access = gl3_image_buffer_begin_data_ptr_access,
	.end_data_ptr_access = gl3_image_buffer_end_data_ptr_access,
};

/* takes ownership of pixels, they get freed when the buffer is destroyed */
static struct wlr_buffer *gl3_image_buffer_create(uint8_t *pixels, int w, int h) {
	struct gl3_image_buffer *buf = calloc(1, sizeof(*buf));
	buf->pixels = pixels;
	buf->stride = (size_t)w * 4;
	wlr_buffer_init(&buf->base, &gl3_image_buffer_impl, w, h);
	return &buf->base;
}

static void set_buffer_opacity(struct wlr_scene_buffer *buffer, int sx, int sy,
		void *data) {
	float *opacity = data;
	wlr_scene_buffer_set_opacity(buffer, *opacity);
}

/* border color and window dimming both reflect focus state */
static void set_toplevel_dimmed(struct gl3_toplevel *toplevel, bool dimmed) {
	float opacity = dimmed ? config.inactive_opacity : 1.0f;
	wlr_scene_node_for_each_buffer(&toplevel->scene_tree->node,
		set_buffer_opacity, &opacity);
	wlr_scene_rect_set_color(toplevel->border,
		dimmed ? config.border_inactive : config.border_active);
}

static void focus_toplevel(struct gl3_toplevel *toplevel) {
	/* only handles keyboard focus, not pointer focus */
	if (toplevel == NULL) {
		return;
	}
	struct gl3_server *server = toplevel->server;
	struct wlr_seat *seat = server->seat;
	struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;
	struct wlr_surface *surface = toplevel->xdg_toplevel->base->surface;
	if (prev_surface == surface) {
		/* already focused, nothing to do */
		return;
	}
	if (prev_surface) {
		/* tell the old surface it lost focus, so it can repaint */
		struct wlr_xdg_toplevel *prev_toplevel =
			wlr_xdg_toplevel_try_from_wlr_surface(prev_surface);
		if (prev_toplevel != NULL) {
			wlr_xdg_toplevel_set_activated(prev_toplevel, false);
			/* base->data is the scene_tree, its node.data is our toplevel */
			struct wlr_scene_tree *prev_tree = prev_toplevel->base->data;
			if (prev_tree != NULL && prev_tree->node.data != NULL) {
				set_toplevel_dimmed(prev_tree->node.data, true);
			}
		}
	}
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
	/* raise it in the scene graph, tiles don't overlap so this is harmless */
	/* the toplevels list order is untouched, so tile positions stay put */
	wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
	/* the raise above would otherwise bury the workspace-dot indicator,
	 * and any top/overlay-layer surface (bars etc), under this window's
	 * content */
	raise_workspace_dots(server);
	raise_layer_shell_overlays(server);
	/* activate the new surface */
	wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, true);
	set_toplevel_dimmed(toplevel, false);
	/* let the seat send key events to this surface from now on */
	if (keyboard != NULL) {
		wlr_seat_keyboard_notify_enter(seat, surface,
			keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
	}
}

/* resizes a window and sizes its border to match, optimistically, so there's
 * no visible flash before the client responds - xdg_toplevel_commit()
 * resyncs the border to whatever size the client actually commits back */
static void resize_toplevel(struct gl3_toplevel *toplevel, int w, int h) {
	wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, w, h);
	wlr_scene_rect_set_size(toplevel->border,
		w + 2 * config.border_width, h + 2 * config.border_width);
}

/* width of the master column for a given output width/gap/ratio, shared by
 * arrange_output() and get_ratio_boundary() so they can't drift apart */
static int compute_master_w(int width, int gap, int ratio) {
	return (width - 3 * gap) * ratio / 10;
}

/* lays out one output's currently-active workspace, dwm-style, first
 * window is master, rest stack, does not touch visibility - arrange_all()
 * handles showing/hiding, since that depends on every output at once */
static void arrange_output(struct gl3_output *output) {
	struct gl3_server *server = output->server;
	/* usable_area is the output's full box minus whatever layer-shell
	 * surfaces (bars etc) have reserved via exclusive_zone - see
	 * arrange_layers(), which keeps this field up to date. defaults to
	 * the full output box when there are no layer surfaces at all */
	struct wlr_box area = output->usable_area;
	if (area.width == 0 || area.height == 0) {
		return;
	}

	int n = 0;
	struct gl3_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->toplevels, link) {
		if (toplevel->workspace == output->workspace) {
			n++;
		}
	}
	if (n == 0) {
		return;
	}

	const int gap = config.gap;

	if (n == 1) {
		wl_list_for_each(toplevel, &server->toplevels, link) {
			if (toplevel->workspace != output->workspace) {
				continue;
			}
			wlr_scene_node_set_position(&toplevel->scene_tree->node,
				area.x + gap, area.y + gap);
			resize_toplevel(toplevel, area.width - 2 * gap, area.height - 2 * gap);
			return;
		}
	}

	int master_w = compute_master_w(area.width, gap, server->master_ratio);
	int stack_w = area.width - 3 * gap - master_w;
	int stack_n = n - 1;
	/* rounding here can leave the last stack window a few px short */
	int stack_h = (area.height - (stack_n + 1) * gap) / stack_n;

	int i = 0;
	wl_list_for_each(toplevel, &server->toplevels, link) {
		if (toplevel->workspace != output->workspace) {
			continue;
		}
		if (i == 0) {
			wlr_scene_node_set_position(&toplevel->scene_tree->node,
				area.x + gap, area.y + gap);
			resize_toplevel(toplevel, master_w, area.height - 2 * gap);
		} else {
			int idx = i - 1;
			wlr_scene_node_set_position(&toplevel->scene_tree->node,
				area.x + master_w + 2 * gap,
				area.y + gap + idx * (stack_h + gap));
			resize_toplevel(toplevel, stack_w, stack_h);
		}
		i++;
	}
}

/* recomputes this output's usable_area from its layer-shell surfaces
 * (background layer first, overlay last, so a higher layer's exclusive
 * zone is reserved out of what's left after lower layers already took
 * their share) and re-tiles into whatever's left - call whenever a layer
 * surface on this output commits, maps, unmaps, or the output resizes */
static void arrange_layers(struct gl3_output *output) {
	struct wlr_box full_area = {0};
	wlr_output_layout_get_box(output->server->output_layout, output->wlr_output, &full_area);
	if (full_area.width == 0 || full_area.height == 0) {
		return;
	}

	struct wlr_box usable = full_area;
	enum zwlr_layer_shell_v1_layer order[] = {
		ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
		ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM,
		ZWLR_LAYER_SHELL_V1_LAYER_TOP,
		ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
	};
	for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); i++) {
		struct gl3_layer_surface *ls;
		wl_list_for_each(ls, &output->layers, link) {
			if (ls->layer_surface->current.layer != order[i]) {
				continue;
			}
			wlr_scene_layer_surface_v1_configure(ls->scene, &full_area, &usable);
		}
	}
	output->usable_area = usable;
	arrange_output(output);
}

/* re-tiles every output's own workspace, and shows/hides every toplevel
 * based on whether its workspace is currently active on any output at all
 * (each workspace shows on at most one output, enforced by switch_workspace,
 * except the pathological 11+ output case where pick_unused_workspace()
 * has to double up - arranged[] below makes sure only one output actually
 * positions a given workspace's windows even then, instead of two outputs
 * writing contradictory positions to the same toplevels) */
static void arrange_all(struct gl3_server *server) {
	struct gl3_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->toplevels, link) {
		bool visible = false;
		struct gl3_output *o;
		wl_list_for_each(o, &server->outputs, link) {
			if (o->workspace == toplevel->workspace) {
				visible = true;
				break;
			}
		}
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, visible);
	}
	bool arranged[GL3_NUM_WORKSPACES + 1] = {0};
	struct gl3_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		if (output->workspace >= 1 && output->workspace <= GL3_NUM_WORKSPACES) {
			if (arranged[output->workspace]) {
				continue;
			}
			arranged[output->workspace] = true;
		}
		arrange_output(output);
	}
}

/* finds the gl3_toplevel for the surface that currently has keyboard focus */
static struct gl3_toplevel *focused_toplevel(struct gl3_server *server) {
	struct wlr_surface *focused = server->seat->keyboard_state.focused_surface;
	struct gl3_toplevel *t;
	wl_list_for_each(t, &server->toplevels, link) {
		if (t->xdg_toplevel->base->surface == focused) {
			return t;
		}
	}
	return NULL;
}

/* the output keybinds like super+1-0 or super+m act on, the output showing
 * the focused window, or if nothing's focused, whichever output the cursor
 * is over, or if that fails too, just the first output - NULL only if no
 * output has connected yet */
static struct gl3_output *get_focused_output(struct gl3_server *server) {
	struct gl3_toplevel *focused = focused_toplevel(server);
	struct gl3_output *o;
	if (focused != NULL) {
		wl_list_for_each(o, &server->outputs, link) {
			if (o->workspace == focused->workspace) {
				return o;
			}
		}
	}
	struct wlr_output *wlr_output = wlr_output_layout_output_at(
		server->output_layout, server->cursor->x, server->cursor->y);
	if (wlr_output != NULL) {
		wl_list_for_each(o, &server->outputs, link) {
			if (o->wlr_output == wlr_output) {
				return o;
			}
		}
	}
	wl_list_for_each(o, &server->outputs, link) {
		return o;
	}
	return NULL;
}

/* moves keyboard focus to the next window on the focused output's workspace */
static void focus_next(struct gl3_server *server) {
	struct gl3_output *output = get_focused_output(server);
	if (output == NULL) {
		return;
	}
	struct gl3_toplevel *current = focused_toplevel(server);
	struct gl3_toplevel *first = NULL;
	struct gl3_toplevel *next = NULL;
	bool seen_current = false;
	struct gl3_toplevel *t;
	wl_list_for_each(t, &server->toplevels, link) {
		if (t->workspace != output->workspace) {
			continue;
		}
		if (first == NULL) {
			first = t;
		}
		if (seen_current && next == NULL) {
			next = t;
		}
		if (t == current) {
			seen_current = true;
		}
	}
	if (next == NULL) {
		/* wrapped around, or nothing was focused, go to the first window */
		next = first;
	}
	focus_toplevel(next);
}

/* moves a toplevel to the front of the whole toplevel list - arrange_output()
 * always treats the first same-workspace entry as master, so this is
 * enough to make it the new master regardless of other workspaces'
 * toplevels interspersed in the list */
static void promote_to_master(struct gl3_toplevel *toplevel) {
	wl_list_remove(&toplevel->link);
	wl_list_insert(&toplevel->server->toplevels, &toplevel->link);
}

/* swaps which window is master, promotes the focused window to master, or
 * if the focused window already is master, promotes the first stack window
 * instead, so the keybind still does something rather than getting stuck
 * once focus lands on master (mirrors dwm's zoom()) */
static void swap_master(struct gl3_server *server) {
	struct gl3_output *output = get_focused_output(server);
	struct gl3_toplevel *focused = focused_toplevel(server);
	if (output == NULL || focused == NULL || focused->workspace != output->workspace) {
		return;
	}
	struct gl3_toplevel *first = NULL;
	struct gl3_toplevel *second = NULL;
	struct gl3_toplevel *t;
	wl_list_for_each(t, &server->toplevels, link) {
		if (t->workspace != output->workspace) {
			continue;
		}
		if (first == NULL) {
			first = t;
		} else if (second == NULL) {
			second = t;
			break;
		}
	}
	struct gl3_toplevel *target = (focused == first) ? second : focused;
	if (target == NULL || target == first) {
		/* only one window on this workspace, nothing to swap */
		return;
	}
	promote_to_master(target);
	arrange_all(server);
	/* keeps focus on target either way, it's a no-op when target was
	 * already focused (the common case), and when target is the promoted
	 * stack window instead, this is what makes focus follow master like
	 * dwm's pop()/zoom() does, instead of leaving focus on the window
	 * that just got demoted into the stack */
	focus_toplevel(target);
}

/* i3-style, if ws is already showing on some output, just move focus there
 * without rearranging anything, otherwise, show it on the focused output,
 * replacing whatever was showing there (which stays assigned to that
 * output, ready to reappear next time it's switched back to) */
static void switch_workspace(struct gl3_server *server, int ws) {
	struct gl3_output *target = NULL;
	struct gl3_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		if (o->workspace == ws) {
			target = o;
			break;
		}
	}
	if (target != NULL && target == get_focused_output(server)) {
		/* already showing on the output you're using, nothing to do */
		return;
	}
	if (target == NULL) {
		target = get_focused_output(server);
		if (target == NULL) {
			/* no outputs connected yet */
			return;
		}
		target->workspace = ws;
		sync_workspace_dots(target);
		arrange_all(server);
	}
	/* focus the first window on that workspace, or clear focus */
	struct gl3_toplevel *t, *first = NULL;
	wl_list_for_each(t, &server->toplevels, link) {
		if (t->workspace == ws) {
			first = t;
			break;
		}
	}
	if (first != NULL) {
		focus_toplevel(first);
	} else {
		wlr_seat_keyboard_notify_clear_focus(server->seat);
	}
}

/* moves the focused window to another workspace, matches i3's default of
 * moving without following, the current workspace stays on screen */
static void move_focused_to_workspace(struct gl3_server *server, int ws) {
	struct gl3_output *origin = get_focused_output(server);
	struct gl3_toplevel *toplevel = focused_toplevel(server);
	if (origin == NULL || toplevel == NULL || toplevel->workspace == ws) {
		return;
	}
	toplevel->workspace = ws;
	arrange_all(server);
	/* the window just left, focus whatever's left on the output it was on */
	struct gl3_toplevel *t, *replacement = NULL;
	wl_list_for_each(t, &server->toplevels, link) {
		if (t->workspace == origin->workspace) {
			replacement = t;
			break;
		}
	}
	if (replacement != NULL) {
		focus_toplevel(replacement);
	} else {
		wlr_seat_keyboard_notify_clear_focus(server->seat);
	}
}

/* runs a shell command detached from the compositor */
static void spawn(const char *cmd) {
	/* no error check on fork, fine here but not for anything critical */
	if (fork() == 0) {
		setsid();
		execl("/bin/sh", "/bin/sh", "-c", cmd, (void *)NULL);
		_exit(EXIT_FAILURE);
	}
}

/* writes the keybind list to a runtime file, so the help keybind can just
 * cat it in a terminal instead of us having to render text on screen */
static void write_help_file(void) {
	const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
	snprintf(gl3_help_path, sizeof(gl3_help_path), "%s/gl3-help.txt",
		runtime_dir != NULL ? runtime_dir : "/tmp");

	FILE *f = fopen(gl3_help_path, "w");
	if (f == NULL) {
		wlr_log(WLR_ERROR, "couldn't write help file %s (%s), super+? will do nothing",
			gl3_help_path, strerror(errno));
		gl3_help_path[0] = '\0';
		return;
	}
	fprintf(f, "gl3 keys\n\n");
	for (size_t i = 0; i < GL3_HELP_NUM_ENTRIES; i++) {
		char name[64];
		xkb_keysym_get_name(*GL3_HELP_ENTRIES[i].key, name, sizeof(name));
		fprintf(f, " super+%-14s %s\n", name, GL3_HELP_ENTRIES[i].label);
	}
	fprintf(f, " super+1 .. super+0   switch workspace\n");
	fprintf(f, " super+shift+1..0     move window to workspace\n");
	fprintf(f, "\npress any key to close\n");
	fclose(f);
}

/* opens a terminal showing the keybind list, does nothing if the help
 * file couldn't be written */
static void show_help(void) {
	if (gl3_help_path[0] == '\0') {
		return;
	}
	char cmd[1536];
	snprintf(cmd, sizeof(cmd), "%s -e sh -c 'cat \"%s\"; read -n1 -s'",
		config.terminal, gl3_help_path);
	spawn(cmd);
}

static void keyboard_handle_modifiers(
		struct wl_listener *listener, void *data) {
	/* fires when shift, alt, etc get pressed or released */
	struct gl3_keyboard *keyboard =
		wl_container_of(listener, keyboard, modifiers);
	/* a seat only has one keyboard, so we swap in whichever one sent this */
	wlr_seat_set_keyboard(keyboard->server->seat, keyboard->wlr_keyboard);
	/* pass the modifier state to the focused client */
	wlr_seat_keyboard_notify_modifiers(keyboard->server->seat,
		&keyboard->wlr_keyboard->modifiers);
}

static bool handle_keybinding(struct gl3_server *server, xkb_keysym_t sym,
		bool shift) {
	/* handles compositor keybinds, assumes super is already held down.
	 * every single-key bind below is configurable (see load_config()) and
	 * matched case-insensitively, since shift changes which keysym a
	 * keycode produces on most layouts */
	if (sym >= XKB_KEY_1 && sym <= XKB_KEY_9) {
		/* super+1-9 - switch workspace, super+shift+1-9 - move window
		 * there, not configurable, tied to the number row like every
		 * other tiling wm */
		int ws = (int)(sym - XKB_KEY_1) + 1;
		if (shift) {
			move_focused_to_workspace(server, ws);
		} else {
			switch_workspace(server, ws);
		}
		return true;
	}
	if (sym == XKB_KEY_0) {
		/* super+0 - switch to the last workspace, super+shift+0 - move
		 * window there */
		if (shift) {
			move_focused_to_workspace(server, GL3_NUM_WORKSPACES);
		} else {
			switch_workspace(server, GL3_NUM_WORKSPACES);
		}
		return true;
	}

	xkb_keysym_t lower = xkb_keysym_to_lower(sym);
	if (lower == xkb_keysym_to_lower(config.key_quit)) {
		wl_display_terminate(server->wl_display);
	} else if (lower == xkb_keysym_to_lower(config.key_terminal)) {
		spawn(config.terminal);
	} else if (lower == xkb_keysym_to_lower(config.key_launcher)) {
		spawn(config.launcher);
	} else if (lower == xkb_keysym_to_lower(config.key_help)) {
		show_help();
	} else if (lower == xkb_keysym_to_lower(config.key_close)) {
		struct wlr_surface *focused =
			server->seat->keyboard_state.focused_surface;
		struct wlr_xdg_toplevel *xt = focused ?
			wlr_xdg_toplevel_try_from_wlr_surface(focused) : NULL;
		if (xt != NULL) {
			wlr_xdg_toplevel_send_close(xt);
		}
	} else if (lower == xkb_keysym_to_lower(config.key_focus_next)) {
		focus_next(server);
	} else if (lower == xkb_keysym_to_lower(config.key_shrink_master)) {
		if (server->master_ratio > 1) {
			server->master_ratio--;
			arrange_all(server);
		}
	} else if (lower == xkb_keysym_to_lower(config.key_grow_master)) {
		if (server->master_ratio < 9) {
			server->master_ratio++;
			arrange_all(server);
		}
	} else if (lower == xkb_keysym_to_lower(config.key_swap_master)) {
		swap_master(server);
	} else {
		return false;
	}
	return true;
}

static void keyboard_handle_key(
		struct wl_listener *listener, void *data) {
	/* fires on every key press and release */
	struct gl3_keyboard *keyboard =
		wl_container_of(listener, keyboard, key);
	struct gl3_server *server = keyboard->server;
	struct wlr_keyboard_key_event *event = data;
	struct wlr_seat *seat = server->seat;

	/* libinput keycode to xkbcommon keycode */
	uint32_t keycode = event->keycode + 8;
	/* look up which keysyms this keycode maps to */
	const xkb_keysym_t *syms;
	int nsyms = xkb_state_key_get_syms(
			keyboard->wlr_keyboard->xkb_state, keycode, &syms);

	bool handled = false;
	uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);
	if ((modifiers & WLR_MODIFIER_LOGO) &&
			event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		/* super is held and this key was pressed, try it as a keybind */
		bool shift = modifiers & WLR_MODIFIER_SHIFT;
		for (int i = 0; i < nsyms; i++) {
			handled = handle_keybinding(server, syms[i], shift);
		}
	}

	if (!handled) {
		/* not a keybind, so pass it through to the client */
		wlr_seat_set_keyboard(seat, keyboard->wlr_keyboard);
		wlr_seat_keyboard_notify_key(seat, event->time_msec,
			event->keycode, event->state);
	}
}

static void keyboard_handle_destroy(struct wl_listener *listener, void *data) {
	/* the underlying keyboard device is gone, clean up */
	struct gl3_keyboard *keyboard =
		wl_container_of(listener, keyboard, destroy);
	wl_list_remove(&keyboard->modifiers.link);
	wl_list_remove(&keyboard->key.link);
	wl_list_remove(&keyboard->destroy.link);
	wl_list_remove(&keyboard->link);
	free(keyboard);
}

static void server_new_keyboard(struct gl3_server *server,
		struct wlr_input_device *device) {
	struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);

	struct gl3_keyboard *keyboard = calloc(1, sizeof(*keyboard));
	keyboard->server = server;
	keyboard->wlr_keyboard = wlr_keyboard;

	/* set up the keymap, assumes defaults like us layout */
	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct xkb_keymap *keymap = xkb_keymap_new_from_names(context, NULL,
		XKB_KEYMAP_COMPILE_NO_FLAGS);

	wlr_keyboard_set_keymap(wlr_keyboard, keymap);
	xkb_keymap_unref(keymap);
	xkb_context_unref(context);
	wlr_keyboard_set_repeat_info(wlr_keyboard, 25, 600);

	/* listen for key and modifier events on this keyboard */
	keyboard->modifiers.notify = keyboard_handle_modifiers;
	wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);
	keyboard->key.notify = keyboard_handle_key;
	wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);
	keyboard->destroy.notify = keyboard_handle_destroy;
	wl_signal_add(&device->events.destroy, &keyboard->destroy);

	wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);

	/* track it in our list of keyboards */
	wl_list_insert(&server->keyboards, &keyboard->link);
}

static void server_new_pointer(struct gl3_server *server,
		struct wlr_input_device *device) {
	/* no special handling, wlr_cursor deals with pointers for us */
	wlr_cursor_attach_input_device(server->cursor, device);
}

static void server_new_input(struct wl_listener *listener, void *data) {
	/* fires when the backend finds a new input device */
	struct gl3_server *server =
		wl_container_of(listener, server, new_input);
	struct wlr_input_device *device = data;
	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		server_new_keyboard(server, device);
		break;
	case WLR_INPUT_DEVICE_POINTER:
		server_new_pointer(server, device);
		break;
	default:
		break;
	}
	/* tell the seat what we support, gl3 always has a cursor */
	uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
	if (!wl_list_empty(&server->keyboards)) {
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	}
	wlr_seat_set_capabilities(server->seat, caps);
}

static void seat_request_cursor(struct wl_listener *listener, void *data) {
	struct gl3_server *server = wl_container_of(
			listener, server, request_cursor);
	/* fires when a client wants to set the cursor image */
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	struct wlr_seat_client *focused_client =
		server->seat->pointer_state.focused_client;
	/* only honor this if the client actually has pointer focus */
	if (focused_client == event->seat_client) {
		/* use the client's surface as the cursor image */
		wlr_cursor_set_surface(server->cursor, event->surface,
				event->hotspot_x, event->hotspot_y);
	}
}

static void seat_pointer_focus_change(struct wl_listener *listener, void *data) {
	struct gl3_server *server = wl_container_of(
			listener, server, pointer_focus_change);
	/* fires when pointer focus moves, including on window close */
	struct wlr_seat_pointer_focus_change_event *event = data;
	if (event->new_surface == NULL) {
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
	}
}

static void seat_request_set_selection(struct wl_listener *listener, void *data) {
	/* fires on copy, gl3 always honors the selection request */
	struct gl3_server *server = wl_container_of(
			listener, server, request_set_selection);
	struct wlr_seat_request_set_selection_event *event = data;
	wlr_seat_set_selection(server->seat, event->source, event->serial);
}

static struct gl3_toplevel *desktop_toplevel_at(
		struct gl3_server *server, double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy) {
	/* finds the topmost surface at these layout coordinates */
	struct wlr_scene_node *node = wlr_scene_node_at(
		&server->scene->tree.node, lx, ly, sx, sy);
	if (node == NULL || node->type != WLR_SCENE_NODE_BUFFER) {
		return NULL;
	}
	struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
	struct wlr_scene_surface *scene_surface =
		wlr_scene_surface_try_from_buffer(scene_buffer);
	if (!scene_surface) {
		return NULL;
	}

	*surface = scene_surface->surface;
	/* walk up to the tree node holding the gl3_toplevel data */
	struct wlr_scene_tree *tree = node->parent;
	while (tree != NULL && tree->node.data == NULL) {
		tree = tree->node.parent;
	}
	return tree->node.data;
}

static void reset_cursor_mode(struct gl3_server *server) {
	/* back to normal, no drag in progress */
	server->cursor_mode = GL3_CURSOR_PASSTHROUGH;
	server->drag_output = NULL;
}

/* finds the output under the cursor and the x position of its master/stack
 * boundary, false if there's no output there or only one window on it
 * right now (no boundary to drag) */
static bool get_ratio_boundary(struct gl3_server *server,
		struct gl3_output **out_output, int *boundary_x) {
	struct wlr_output *wlr_output = wlr_output_layout_output_at(
		server->output_layout, server->cursor->x, server->cursor->y);
	if (wlr_output == NULL) {
		return false;
	}
	struct gl3_output *output = NULL;
	struct gl3_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		if (o->wlr_output == wlr_output) {
			output = o;
			break;
		}
	}
	if (output == NULL) {
		return false;
	}
	struct wlr_box area = {0};
	wlr_output_layout_get_box(server->output_layout, wlr_output, &area);
	if (area.width == 0) {
		return false;
	}
	int n = 0;
	struct gl3_toplevel *t;
	wl_list_for_each(t, &server->toplevels, link) {
		if (t->workspace == output->workspace) {
			n++;
		}
	}
	if (n < 2) {
		return false;
	}
	int gap = config.gap;
	int master_w = compute_master_w(area.width, gap, server->master_ratio);
	/* middle of the gap strip between the master and stack columns */
	*boundary_x = area.x + gap + master_w + gap / 2;
	*out_output = output;
	return true;
}

/* live-updates the master ratio to follow the cursor while dragging */
static void update_ratio_drag(struct gl3_server *server) {
	struct wlr_box area = {0};
	wlr_output_layout_get_box(server->output_layout,
		server->drag_output->wlr_output, &area);
	if (area.width == 0) {
		return;
	}
	int ratio = (int)(10.0 * (server->cursor->x - area.x) / area.width + 0.5);
	if (ratio < 1) {
		ratio = 1;
	} else if (ratio > 9) {
		ratio = 9;
	}
	if (ratio != server->master_ratio) {
		server->master_ratio = ratio;
		arrange_all(server);
	}
}

static void process_cursor_motion(struct gl3_server *server, uint32_t time) {
	/* dragging the master/stack boundary, handle that instead */
	if (server->cursor_mode == GL3_CURSOR_RATIO_DRAG) {
		update_ratio_drag(server);
		return;
	}

	/* otherwise find what's under the cursor and forward the event */
	double sx, sy;
	struct wlr_seat *seat = server->seat;
	struct wlr_surface *surface = NULL;
	struct gl3_toplevel *toplevel = desktop_toplevel_at(server,
			server->cursor->x, server->cursor->y, &surface, &sx, &sy);
	if (!toplevel) {
		/* nothing under the cursor, show the default cursor image */
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
	}
	if (surface) {
		/* enter gives pointer focus, separate from keyboard focus */
		wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
		wlr_seat_pointer_notify_motion(seat, time, sx, sy);
	} else {
		/* nothing under the cursor, clear pointer focus */
		wlr_seat_pointer_clear_focus(seat);
	}
}

static void server_cursor_motion(struct wl_listener *listener, void *data) {
	/* fires on relative pointer motion, ie a delta */
	struct gl3_server *server =
		wl_container_of(listener, server, cursor_motion);
	struct wlr_pointer_motion_event *event = data;
	/* moves the cursor, wlr_cursor clamps it to the output layout */
	wlr_cursor_move(server->cursor, &event->pointer->base,
			event->delta_x, event->delta_y);
	process_cursor_motion(server, event->time_msec);
}

static void server_cursor_motion_absolute(
		struct wl_listener *listener, void *data) {
	/* fires on absolute pointer motion, ie nested window or a tablet */
	struct gl3_server *server =
		wl_container_of(listener, server, cursor_motion_absolute);
	struct wlr_pointer_motion_absolute_event *event = data;
	wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x,
		event->y);
	process_cursor_motion(server, event->time_msec);
}

static void server_cursor_button(struct wl_listener *listener, void *data) {
	/* fires on mouse button press or release */
	struct gl3_server *server =
		wl_container_of(listener, server, cursor_button);
	struct wlr_pointer_button_event *event = data;
	/* tell the focused client about the button event */
	wlr_seat_pointer_notify_button(server->seat,
			event->time_msec, event->button, event->state);
	if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
		/* releasing ends a boundary drag, if one was happening */
		reset_cursor_mode(server);
	} else {
		/* pressed on the master/stack boundary, start a ratio drag */
		struct gl3_output *boundary_output;
		int boundary_x;
		if (get_ratio_boundary(server, &boundary_output, &boundary_x) &&
				fabs(server->cursor->x - boundary_x) <= GL3_DRAG_THRESHOLD) {
			server->cursor_mode = GL3_CURSOR_RATIO_DRAG;
			server->drag_output = boundary_output;
			return;
		}
		/* otherwise focus whatever is under the cursor */
		double sx, sy;
		struct wlr_surface *surface = NULL;
		struct gl3_toplevel *toplevel = desktop_toplevel_at(server,
				server->cursor->x, server->cursor->y, &surface, &sx, &sy);
		focus_toplevel(toplevel);
	}
}

static void server_cursor_axis(struct wl_listener *listener, void *data) {
	/* fires on scroll wheel movement */
	struct gl3_server *server =
		wl_container_of(listener, server, cursor_axis);
	struct wlr_pointer_axis_event *event = data;
	/* pass the scroll event to the focused client */
	wlr_seat_pointer_notify_axis(server->seat,
			event->time_msec, event->orientation, event->delta,
			event->delta_discrete, event->source, event->relative_direction);
}

static void server_cursor_frame(struct wl_listener *listener, void *data) {
	/* groups pointer events sent at the same time */
	struct gl3_server *server =
		wl_container_of(listener, server, cursor_frame);
	/* pass the frame event to the focused client */
	wlr_seat_pointer_notify_frame(server->seat);
}

static void output_frame(struct wl_listener *listener, void *data) {
	/* fires when an output is ready for a new frame, usually at 60hz */
	struct gl3_output *output = wl_container_of(listener, output, frame);
	struct wlr_scene *scene = output->server->scene;

	struct wlr_scene_output *scene_output = wlr_scene_get_scene_output(
		scene, output->wlr_output);

	/* render if needed and push the frame to the screen */
	wlr_scene_output_commit(scene_output, NULL);

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	wlr_scene_output_send_frame_done(scene_output, &now);
}

/* (re)sizes and positions this output's own wallpaper node to match its
 * current box in the output layout - called both when the output first
 * connects and again whenever its state changes, so a resolution change
 * (docks, VMs renegotiating a mode) doesn't leave a stale-sized wallpaper
 * behind on an output that was already connected */
static void sync_output_bg(struct gl3_output *output) {
	if (output->server->bg_buffer == NULL) {
		return;
	}
	struct wlr_box obox;
	wlr_output_layout_get_box(output->server->output_layout, output->wlr_output, &obox);
	if (obox.width <= 0 || obox.height <= 0) {
		return;
	}
	if (output->bg == NULL) {
		output->bg = wlr_scene_buffer_create(&output->server->scene->tree,
			output->server->bg_buffer);
		wlr_scene_node_lower_to_bottom(&output->bg->node);
	}
	wlr_scene_buffer_set_dest_size(output->bg, obox.width, obox.height);
	wlr_scene_node_set_position(&output->bg->node, obox.x, obox.y);
}

/* raises every output's workspace-dot row back above whatever just got
 * raised over it (window tiles get raised to top on focus, which would
 * otherwise bury the indicator under window content) - cheap enough to
 * call on every focus change, there's at most GL3_NUM_WORKSPACES dots
 * per output */
static void raise_workspace_dots(struct gl3_server *server) {
	struct gl3_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		for (int i = 0; i < GL3_NUM_WORKSPACES; i++) {
			if (output->workspace_dots[i] != NULL) {
				wlr_scene_node_raise_to_top(&output->workspace_dots[i]->node);
			}
		}
	}
}

/* (re)creates (bottom-left corner, one 6px square per workspace) and
 * recolors this output's workspace-dot row - the only on-screen indicator
 * of which workspace is showing, since gl3 has no text rendering. call
 * whenever output->workspace changes, or once at output-connect time so
 * the row exists even before any window ever gets tiled on it */
static void sync_workspace_dots(struct gl3_output *output) {
	struct wlr_box obox;
	wlr_output_layout_get_box(output->server->output_layout, output->wlr_output, &obox);
	if (obox.width <= 0 || obox.height <= 0) {
		return;
	}
	const int dot_size = 6;
	const int dot_gap = 4;
	const int margin = 8;
	for (int i = 0; i < GL3_NUM_WORKSPACES; i++) {
		if (output->workspace_dots[i] == NULL) {
			output->workspace_dots[i] = wlr_scene_rect_create(
				&output->server->scene->tree, dot_size, dot_size, config.border_inactive);
			wlr_scene_node_set_position(&output->workspace_dots[i]->node,
				obox.x + margin + i * (dot_size + dot_gap),
				obox.y + obox.height - margin - dot_size);
		}
		bool active = (i + 1) == output->workspace;
		wlr_scene_rect_set_color(output->workspace_dots[i],
			active ? config.border_active : config.border_inactive);
	}
	raise_workspace_dots(output->server);
	raise_layer_shell_overlays(output->server);
}

static void output_request_state(struct wl_listener *listener, void *data) {
	/* fires when the backend wants a new output state, eg on resize */
	struct gl3_output *output = wl_container_of(listener, output, request_state);
	const struct wlr_output_event_request_state *event = data;
	wlr_output_commit_state(output->wlr_output, event->state);
	sync_output_bg(output);
	sync_workspace_dots(output);
	/* output size may have just changed - usable_area and any layer
	 * surface anchored full-width/full-height (most bars) need to be
	 * recomputed, which also re-tiles this output's windows into the
	 * new size */
	arrange_layers(output);
}

static void output_destroy(struct wl_listener *listener, void *data) {
	struct gl3_output *output = wl_container_of(listener, output, destroy);
	struct gl3_server *server = output->server;

	/* an in-progress ratio drag on this output no longer has anywhere to
	 * act on */
	if (server->drag_output == output) {
		reset_cursor_mode(server);
	}

	/* the per-output wallpaper node isn't part of the output's own scene
	 * subtree (it's a direct child of the root scene tree, positioned to
	 * match this output), so it doesn't get cleaned up automatically */
	if (output->bg != NULL) {
		wlr_scene_node_destroy(&output->bg->node);
	}
	for (int i = 0; i < GL3_NUM_WORKSPACES; i++) {
		if (output->workspace_dots[i] != NULL) {
			wlr_scene_node_destroy(&output->workspace_dots[i]->node);
		}
	}

	/* destroy this output's layer surfaces (bars etc) before freeing it -
	 * their own destroy handler removes them from output->layers, which
	 * lives inside this struct, so that has to happen before free() below
	 * or their eventual destroy would write into already-freed memory */
	struct gl3_layer_surface *ls, *ls_tmp;
	wl_list_for_each_safe(ls, ls_tmp, &output->layers, link) {
		wlr_layer_surface_v1_destroy(ls->layer_surface);
	}

	wl_list_remove(&output->frame.link);
	wl_list_remove(&output->request_state.link);
	wl_list_remove(&output->destroy.link);
	wl_list_remove(&output->link);
	free(output);

	/* this output's workspace isn't shown anywhere now - hide whatever was
	 * on it rather than leaving it positioned for a monitor that's gone,
	 * switching to that workspace on any remaining output (super+n) brings
	 * it back, same as switching to any other hidden workspace does */
	arrange_all(server);
}

/* the lowest-numbered workspace not already showing on another output, so
 * a freshly connected monitor gets its own workspace instead of doubling
 * up on one that's already in use - falls back to 1 if all are taken
 * (11+ monitors) */
static int pick_unused_workspace(struct gl3_server *server) {
	for (int ws = 1; ws <= GL3_NUM_WORKSPACES; ws++) {
		bool taken = false;
		struct gl3_output *o;
		wl_list_for_each(o, &server->outputs, link) {
			if (o->workspace == ws) {
				taken = true;
				break;
			}
		}
		if (!taken) {
			return ws;
		}
	}
	return 1;
}

static void server_new_output(struct wl_listener *listener, void *data) {
	/* fires when a new monitor or display becomes available */
	struct gl3_server *server =
		wl_container_of(listener, server, new_output);
	struct wlr_output *wlr_output = data;

	/* wire the output up to our renderer and allocator */
	wlr_output_init_render(wlr_output, server->allocator, server->renderer);

	/* the output may start disabled, so turn it on */
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, true);

	/* pick the monitor's preferred mode, we don't let users choose yet */
	struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
	if (mode != NULL) {
		wlr_output_state_set_mode(&state, mode);
	}

	/* apply the new state all at once */
	wlr_output_commit_state(wlr_output, &state);
	wlr_output_state_finish(&state);

	/* track this output in our own struct */
	struct gl3_output *output = calloc(1, sizeof(*output));
	output->wlr_output = wlr_output;
	/* lets server_new_layer_surface() find this struct back from just the
	 * wlr_output the protocol handed it (eg layer_surface->output) */
	wlr_output->data = output;
	output->server = server;
	/* before inserting into server->outputs, so it doesn't count itself */
	output->workspace = pick_unused_workspace(server);
	wl_list_init(&output->layers);

	/* listen for frame, state, and destroy events */
	output->frame.notify = output_frame;
	wl_signal_add(&wlr_output->events.frame, &output->frame);

	output->request_state.notify = output_request_state;
	wl_signal_add(&wlr_output->events.request_state, &output->request_state);

	output->destroy.notify = output_destroy;
	wl_signal_add(&wlr_output->events.destroy, &output->destroy);

	wl_list_insert(&server->outputs, &output->link);

	/* add_auto arranges outputs left to right, no user config yet */
	struct wlr_output_layout_output *l_output = wlr_output_layout_add_auto(server->output_layout,
		wlr_output);
	struct wlr_scene_output *scene_output = wlr_scene_output_create(server->scene, wlr_output);
	wlr_scene_output_layout_add_output(server->scene_layout, l_output, scene_output);

	/* give this output its own copy of the wallpaper, scaled (on the gpu,
	 * via dest_size, no cpu resampling needed) to exactly this output's
	 * size and positioned at its spot in the layout - each output gets
	 * this regardless of its resolution or position relative to others */
	sync_output_bg(output);
	sync_workspace_dots(output);
	/* no layer surfaces exist for this output yet, but this still needs
	 * to run once so usable_area starts out as the full output box
	 * instead of the zeroed struct calloc() left it with */
	arrange_layers(output);
}

static void xdg_toplevel_map(struct wl_listener *listener, void *data) {
	/* fires when the window is ready to show on screen */
	struct gl3_toplevel *toplevel = wl_container_of(listener, toplevel, map);

	/* new windows open on whatever workspace is showing on the focused
	 * output right now, or workspace 1 in the never-happens case of a
	 * client mapping before any output has connected */
	struct gl3_output *output = get_focused_output(toplevel->server);
	toplevel->workspace = output != NULL ? output->workspace : 1;
	wl_list_insert(&toplevel->server->toplevels, &toplevel->link);

	arrange_all(toplevel->server);
	focus_toplevel(toplevel);
}

static void xdg_toplevel_unmap(struct wl_listener *listener, void *data) {
	/* fires when the window should no longer be shown */
	struct gl3_toplevel *toplevel = wl_container_of(listener, toplevel, unmap);

	/* a window disappearing mid-drag can drop its workspace below the
	 * two-window minimum a ratio drag needs, but only cancel the drag if
	 * it's actually dragging the output this window was showing on -
	 * a window closing on some other monitor/workspace shouldn't abort
	 * an unrelated drag in progress elsewhere */
	if (toplevel->server->cursor_mode == GL3_CURSOR_RATIO_DRAG &&
			toplevel->server->drag_output != NULL &&
			toplevel->workspace == toplevel->server->drag_output->workspace) {
		reset_cursor_mode(toplevel->server);
	}

	wl_list_remove(&toplevel->link);

	arrange_all(toplevel->server);
}

static void xdg_toplevel_commit(struct wl_listener *listener, void *data) {
	/* fires when the client commits a new surface state */
	struct gl3_toplevel *toplevel = wl_container_of(listener, toplevel, commit);

	if (toplevel->xdg_toplevel->base->initial_commit) {
		/* first commit needs a reply, let the client pick its own size */
		wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, 0, 0);
		return;
	}

	/* resize_toplevel() sizes the border optimistically to the requested
	 * size so there's no visible flash, but the client can commit back a
	 * different size (min/max clamping, async toolkits) - resync here to
	 * whatever geometry the client actually committed, every commit */
	struct wlr_box *geo = &toplevel->xdg_toplevel->base->geometry;
	if (geo->width > 0 && geo->height > 0) {
		wlr_scene_rect_set_size(toplevel->border,
			geo->width + 2 * config.border_width,
			geo->height + 2 * config.border_width);
	}
}

static void xdg_toplevel_destroy(struct wl_listener *listener, void *data) {
	/* fires when the window is destroyed */
	struct gl3_toplevel *toplevel = wl_container_of(listener, toplevel, destroy);

	wl_list_remove(&toplevel->map.link);
	wl_list_remove(&toplevel->unmap.link);
	wl_list_remove(&toplevel->commit.link);
	wl_list_remove(&toplevel->destroy.link);
	wl_list_remove(&toplevel->request_move.link);
	wl_list_remove(&toplevel->request_resize.link);
	wl_list_remove(&toplevel->request_maximize.link);
	wl_list_remove(&toplevel->request_fullscreen.link);

	free(toplevel);
}

static void xdg_toplevel_request_move(
		struct wl_listener *listener, void *data) {
	/* gl3 has no floating windows, so free-form move makes no sense here,
	 * ignored on purpose, drag the master/stack boundary to resize instead */
}

static void xdg_toplevel_request_resize(
		struct wl_listener *listener, void *data) {
	/* gl3 has no floating windows, honoring this would desync the border
	 * from the tile since only arrange_output() keeps them in sync,
	 * ignored on purpose, drag the master/stack boundary to resize instead */
}

static void xdg_toplevel_request_maximize(
		struct wl_listener *listener, void *data) {
	/* gl3 doesn't maximize windows, but still must reply per protocol */
	struct gl3_toplevel *toplevel =
		wl_container_of(listener, toplevel, request_maximize);
	if (toplevel->xdg_toplevel->base->initialized) {
		wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
	}
}

static void xdg_toplevel_request_fullscreen(
		struct wl_listener *listener, void *data) {
	/* same as request_maximize, must still send a reply */
	struct gl3_toplevel *toplevel =
		wl_container_of(listener, toplevel, request_fullscreen);
	if (toplevel->xdg_toplevel->base->initialized) {
		wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
	}
}

static void server_new_xdg_toplevel(struct wl_listener *listener, void *data) {
	/* fires when a client opens a new application window */
	struct gl3_server *server = wl_container_of(listener, server, new_xdg_toplevel);
	struct wlr_xdg_toplevel *xdg_toplevel = data;

	/* make a gl3_toplevel to track this window */
	struct gl3_toplevel *toplevel = calloc(1, sizeof(*toplevel));
	toplevel->server = server;
	toplevel->xdg_toplevel = xdg_toplevel;
	toplevel->scene_tree =
		wlr_scene_xdg_surface_create(&toplevel->server->scene->tree, xdg_toplevel->base);
	toplevel->scene_tree->node.data = toplevel;
	xdg_toplevel->base->data = toplevel->scene_tree;

	/* border lives inside the same tree, so it moves and raises with it */
	toplevel->border = wlr_scene_rect_create(toplevel->scene_tree, 1, 1,
		config.border_inactive);
	wlr_scene_node_lower_to_bottom(&toplevel->border->node);
	wlr_scene_node_set_position(&toplevel->border->node,
		-config.border_width, -config.border_width);

	/* listen for the events this window can emit */
	toplevel->map.notify = xdg_toplevel_map;
	wl_signal_add(&xdg_toplevel->base->surface->events.map, &toplevel->map);
	toplevel->unmap.notify = xdg_toplevel_unmap;
	wl_signal_add(&xdg_toplevel->base->surface->events.unmap, &toplevel->unmap);
	toplevel->commit.notify = xdg_toplevel_commit;
	wl_signal_add(&xdg_toplevel->base->surface->events.commit, &toplevel->commit);

	toplevel->destroy.notify = xdg_toplevel_destroy;
	wl_signal_add(&xdg_toplevel->events.destroy, &toplevel->destroy);

	/* move, resize, maximize, fullscreen requests */
	toplevel->request_move.notify = xdg_toplevel_request_move;
	wl_signal_add(&xdg_toplevel->events.request_move, &toplevel->request_move);
	toplevel->request_resize.notify = xdg_toplevel_request_resize;
	wl_signal_add(&xdg_toplevel->events.request_resize, &toplevel->request_resize);
	toplevel->request_maximize.notify = xdg_toplevel_request_maximize;
	wl_signal_add(&xdg_toplevel->events.request_maximize, &toplevel->request_maximize);
	toplevel->request_fullscreen.notify = xdg_toplevel_request_fullscreen;
	wl_signal_add(&xdg_toplevel->events.request_fullscreen, &toplevel->request_fullscreen);
}

static void xdg_popup_commit(struct wl_listener *listener, void *data) {
	/* fires when the popup commits a new surface state */
	struct gl3_popup *popup = wl_container_of(listener, popup, commit);

	if (popup->xdg_popup->base->initial_commit) {
		/* first commit needs a reply, gl3 sends an empty one */
		wlr_xdg_surface_schedule_configure(popup->xdg_popup->base);
	}
}

static void xdg_popup_destroy(struct wl_listener *listener, void *data) {
	/* fires when the popup is destroyed */
	struct gl3_popup *popup = wl_container_of(listener, popup, destroy);

	wl_list_remove(&popup->commit.link);
	wl_list_remove(&popup->destroy.link);

	free(popup);
}

static void server_new_xdg_popup(struct wl_listener *listener, void *data) {
	/* fires when a client opens a new popup, eg a context menu */
	struct wlr_xdg_popup *xdg_popup = data;

	struct gl3_popup *popup = calloc(1, sizeof(*popup));
	popup->xdg_popup = xdg_popup;

	/* popups need adding to the scene graph under their parent - the
	 * parent is usually an xdg_toplevel or another xdg_popup, but with
	 * wlr-layer-shell it can also be a layer surface (eg a bar's dropdown
	 * menu), which isn't an xdg_surface at all, so that needs its own
	 * lookup - both kinds stash their own scene tree in surface->data */
	struct wlr_scene_tree *parent_tree = NULL;
	struct wlr_xdg_surface *xdg_parent =
		wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
	if (xdg_parent != NULL) {
		parent_tree = xdg_parent->data;
	} else {
		struct wlr_layer_surface_v1 *layer_parent =
			wlr_layer_surface_v1_try_from_wlr_surface(xdg_popup->parent);
		if (layer_parent != NULL) {
			parent_tree = layer_parent->data;
		}
	}
	assert(parent_tree != NULL);
	xdg_popup->base->data = wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);

	popup->commit.notify = xdg_popup_commit;
	wl_signal_add(&xdg_popup->base->surface->events.commit, &popup->commit);

	popup->destroy.notify = xdg_popup_destroy;
	wl_signal_add(&xdg_popup->events.destroy, &popup->destroy);
}

static void xdg_toplevel_decoration_commit(struct wl_listener *listener, void *data) {
	/* only wired up while waiting on a toplevel's first commit (see
	 * force_decoration_server_side()) - wlr_xdg_surface_schedule_configure(),
	 * which set_mode() calls into, asserts if the surface isn't
	 * initialized yet, so this waits for that, then forces the mode and
	 * detaches itself, it's only needed once */
	struct gl3_decoration *decoration = wl_container_of(listener, decoration, commit);
	if (!decoration->wlr_decoration->toplevel->base->initialized) {
		return;
	}
	wl_list_remove(&decoration->commit.link);
	decoration->has_commit_listener = false;
	wlr_xdg_toplevel_decoration_v1_set_mode(decoration->wlr_decoration,
		WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

/* gl3 has no client-side-decoration mode to offer, so this always forces
 * server-side (ie the client draws nothing, gl3's own border is it) -
 * called both right when the decoration object is created and whenever the
 * client explicitly requests a mode, since either can happen first. set_mode()
 * can't be called before the toplevel's first commit (wlroots asserts), and
 * some clients (eg foot) create the decoration object and request a mode
 * immediately, before ever committing - if that's where things stand, this
 * defers to xdg_toplevel_decoration_commit() instead of calling directly */
static void force_decoration_server_side(struct gl3_decoration *decoration) {
	if (decoration->wlr_decoration->toplevel->base->initialized) {
		wlr_xdg_toplevel_decoration_v1_set_mode(decoration->wlr_decoration,
			WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
	} else if (!decoration->has_commit_listener) {
		decoration->commit.notify = xdg_toplevel_decoration_commit;
		wl_signal_add(&decoration->wlr_decoration->toplevel->base->surface->events.commit,
			&decoration->commit);
		decoration->has_commit_listener = true;
	}
	/* else: already waiting on the commit listener above, nothing more to do */
}

static void xdg_toplevel_decoration_request_mode(
		struct wl_listener *listener, void *data) {
	/* fires if the client asks for client-side decorations (or anything
	 * else) - always say no, see force_decoration_server_side() */
	struct gl3_decoration *decoration =
		wl_container_of(listener, decoration, request_mode);
	force_decoration_server_side(decoration);
}

static void xdg_toplevel_decoration_destroy(
		struct wl_listener *listener, void *data) {
	struct gl3_decoration *decoration =
		wl_container_of(listener, decoration, destroy);
	wl_list_remove(&decoration->request_mode.link);
	wl_list_remove(&decoration->destroy.link);
	if (decoration->has_commit_listener) {
		wl_list_remove(&decoration->commit.link);
	}
	free(decoration);
}

static void server_new_xdg_toplevel_decoration(
		struct wl_listener *listener, void *data) {
	/* fires when a client creates a decoration object for one of its
	 * windows - immediately tell it server-side (ie draw nothing), gl3
	 * has no client-side-decoration mode to offer */
	struct wlr_xdg_toplevel_decoration_v1 *wlr_decoration = data;

	struct gl3_decoration *decoration = calloc(1, sizeof(*decoration));
	decoration->wlr_decoration = wlr_decoration;

	decoration->request_mode.notify = xdg_toplevel_decoration_request_mode;
	wl_signal_add(&wlr_decoration->events.request_mode, &decoration->request_mode);

	decoration->destroy.notify = xdg_toplevel_decoration_destroy;
	wl_signal_add(&wlr_decoration->events.destroy, &decoration->destroy);

	force_decoration_server_side(decoration);
}

/* raises the top and overlay layer-shell scene trees back above whatever
 * just got raised over them - toplevels get raised on every focus change
 * (see focus_toplevel()), which would otherwise bury a bar/launcher under
 * window content since they're all children of the same root scene tree */
static void raise_layer_shell_overlays(struct gl3_server *server) {
	wlr_scene_node_raise_to_top(&server->layer_tree[ZWLR_LAYER_SHELL_V1_LAYER_TOP]->node);
	wlr_scene_node_raise_to_top(&server->layer_tree[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY]->node);
}

static void layer_surface_map(struct wl_listener *listener, void *data) {
	struct gl3_layer_surface *ls = wl_container_of(listener, ls, map);
	struct gl3_server *server = ls->output->server;

	/* most layer surfaces (status bars) don't request keyboard
	 * interactivity at all and shouldn't steal focus from whatever
	 * toplevel is in use - only hand focus over if this one asked for it */
	if (ls->layer_surface->current.keyboard_interactive !=
			ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE) {
		struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
		if (keyboard != NULL) {
			wlr_seat_keyboard_notify_enter(server->seat, ls->layer_surface->surface,
				keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
		}
	}
}

static void layer_surface_unmap(struct wl_listener *listener, void *data) {
	struct gl3_layer_surface *ls = wl_container_of(listener, ls, unmap);
	struct gl3_server *server = ls->output->server;

	/* don't leave the seat's keyboard focus pointing at a surface that's
	 * no longer showing anything */
	if (server->seat->keyboard_state.focused_surface == ls->layer_surface->surface) {
		wlr_seat_keyboard_notify_clear_focus(server->seat);
	}

	arrange_layers(ls->output);
}

static void layer_surface_commit(struct wl_listener *listener, void *data) {
	struct gl3_layer_surface *ls = wl_container_of(listener, ls, commit);

	/* reconfiguring sends a fresh configure event, which the client acks
	 * and then commits again in response - if that commit reconfigured
	 * unconditionally too, an ordinary "just redrew the same buffer"
	 * commit would loop forever (configure -> ack -> commit -> configure
	 * -> ...). only reconfigure on the first commit (nothing's been
	 * configured yet at all) or when the client actually asked for
	 * something layout-affecting to change */
	const uint32_t layout_affecting =
		WLR_LAYER_SURFACE_V1_STATE_DESIRED_SIZE |
		WLR_LAYER_SURFACE_V1_STATE_ANCHOR |
		WLR_LAYER_SURFACE_V1_STATE_EXCLUSIVE_ZONE |
		WLR_LAYER_SURFACE_V1_STATE_MARGIN;
	if (ls->layer_surface->initial_commit ||
			(ls->layer_surface->current.committed & layout_affecting) != 0) {
		arrange_layers(ls->output);
	}
}

static void layer_surface_destroy(struct wl_listener *listener, void *data) {
	struct gl3_layer_surface *ls = wl_container_of(listener, ls, destroy);
	struct gl3_output *output = ls->output;

	wl_list_remove(&ls->link);
	wl_list_remove(&ls->map.link);
	wl_list_remove(&ls->unmap.link);
	wl_list_remove(&ls->destroy.link);
	wl_list_remove(&ls->commit.link);
	free(ls);

	arrange_layers(output);
}

static void server_new_layer_surface(struct wl_listener *listener, void *data) {
	/* fires when a client opens a bar/launcher/lock-screen/etc via the
	 * wlr-layer-shell-v1 protocol */
	struct gl3_server *server =
		wl_container_of(listener, server, new_layer_surface);
	struct wlr_layer_surface_v1 *layer_surface = data;

	/* the protocol lets a client leave the output unset and asks the
	 * compositor to pick one - land it on whichever output currently has
	 * focus, or just the first output if none does yet */
	if (layer_surface->output == NULL) {
		struct gl3_output *fallback = get_focused_output(server);
		if (fallback == NULL && !wl_list_empty(&server->outputs)) {
			fallback = wl_container_of(server->outputs.next, fallback, link);
		}
		if (fallback == NULL) {
			/* no outputs connected at all yet - nowhere to put this */
			wlr_layer_surface_v1_destroy(layer_surface);
			return;
		}
		layer_surface->output = fallback->wlr_output;
	}

	struct gl3_output *output = layer_surface->output->data;
	assert(output != NULL);

	struct gl3_layer_surface *ls = calloc(1, sizeof(*ls));
	ls->output = output;
	ls->layer_surface = layer_surface;

	/* the scene tree it's attached to here is fixed at creation - the
	 * protocol technically allows a client to request a different layer
	 * later on, which gl3 doesn't re-parent for (rare in practice, no
	 * real bar/launcher does this), it'd just keep rendering in its
	 * original stacking position */
	uint32_t layer = layer_surface->pending.layer;
	if (layer > ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY) {
		layer = ZWLR_LAYER_SHELL_V1_LAYER_TOP;
	}
	ls->scene = wlr_scene_layer_surface_v1_create(
		server->layer_tree[layer], layer_surface);
	/* mirrors xdg_toplevel/xdg_popup's own base->data convention, so
	 * server_new_xdg_popup() can find this surface's scene tree the same
	 * way it already does for toplevel-parented popups */
	layer_surface->data = ls->scene->tree;

	wl_list_insert(&output->layers, &ls->link);

	ls->map.notify = layer_surface_map;
	wl_signal_add(&layer_surface->surface->events.map, &ls->map);

	ls->unmap.notify = layer_surface_unmap;
	wl_signal_add(&layer_surface->surface->events.unmap, &ls->unmap);

	ls->destroy.notify = layer_surface_destroy;
	wl_signal_add(&layer_surface->events.destroy, &ls->destroy);

	ls->commit.notify = layer_surface_commit;
	wl_signal_add(&layer_surface->surface->events.commit, &ls->commit);

	/* no arrange_layers() call here - the surface isn't initialized yet
	 * at this point (before its first commit), calling
	 * wlr_layer_surface_v1_configure() this early hits a wlroots
	 * assertion. layer_surface_commit() above covers the first commit
	 * too, that's the earliest point configuring it is actually valid */
}

int main(int argc, char *argv[]) {
	wlr_log_init(WLR_DEBUG, NULL);
	/* reads ~/.config/gl3/gl3.conf, falls back to the defaults above */
	load_config();
	check_keybind_collisions();
	/* auto-reap spawned children, so terminals don't turn into zombies */
	signal(SIGCHLD, SIG_IGN);
	char *startup_cmd = NULL;

	int c;
	while ((c = getopt(argc, argv, "s:h")) != -1) {
		switch (c) {
		case 's':
			startup_cmd = optarg;
			break;
		default:
			printf("usage: %s [-s startup command]\n", argv[0]);
			return 0;
		}
	}
	if (optind < argc) {
		printf("usage: %s [-s startup command]\n", argv[0]);
		return 0;
	}

	struct gl3_server server = {0};
	server.master_ratio = config.master_ratio;
	/* libwayland manages the display, sockets, and globals for us */
	server.wl_display = wl_display_create();
	/* picks the best backend for the environment, eg x11 window vs real drm */
	server.backend = wlr_backend_autocreate(wl_display_get_event_loop(server.wl_display), NULL);
	if (server.backend == NULL) {
		wlr_log(WLR_ERROR, "failed to create wlr_backend");
		return 1;
	}

	/* picks pixman, gles2, or vulkan, overridable with the wlr_renderer env var */
	server.renderer = wlr_renderer_autocreate(server.backend);
	if (server.renderer == NULL) {
		wlr_log(WLR_ERROR, "failed to create wlr_renderer");
		return 1;
	}

	wlr_renderer_init_wl_display(server.renderer, server.wl_display);

	/* bridges the renderer and backend, handles buffer creation */
	server.allocator = wlr_allocator_autocreate(server.backend,
		server.renderer);
	if (server.allocator == NULL) {
		wlr_log(WLR_ERROR, "failed to create wlr_allocator");
		return 1;
	}

	/* compositor lets clients make surfaces, this also sets up the clipboard */
	wlr_compositor_create(server.wl_display, 5, server.renderer);
	wlr_subcompositor_create(server.wl_display);
	wlr_data_device_manager_create(server.wl_display);

	/* tracks the physical arrangement of screens */
	server.output_layout = wlr_output_layout_create(server.wl_display);

	/* listen for new monitors becoming available */
	wl_list_init(&server.outputs);
	server.new_output.notify = server_new_output;
	wl_signal_add(&server.backend->events.new_output, &server.new_output);

	/* the scene graph handles rendering and damage tracking for us */
	server.scene = wlr_scene_create();
	server.scene_layout = wlr_scene_attach_output_layout(server.scene, server.output_layout);

	/* one scene subtree per wlr-layer-shell layer, created in stacking
	 * order (background lowest, overlay highest) - toplevels get added
	 * later as direct children of server.scene->tree too, which would
	 * otherwise land them above top/overlay by insertion order alone,
	 * see raise_layer_shell_overlays() for how that's kept correct */
	for (size_t i = 0; i < sizeof(server.layer_tree) / sizeof(server.layer_tree[0]); i++) {
		server.layer_tree[i] = wlr_scene_tree_create(&server.scene->tree);
	}
	server.layer_shell = wlr_layer_shell_v1_create(server.wl_display, 4);
	server.new_layer_surface.notify = server_new_layer_surface;
	wl_signal_add(&server.layer_shell->events.new_surface, &server.new_layer_surface);

	/* wallpaper, falls back to a solid crimson color if it fails to load,
	 * no outputs exist yet at this point (new_output only fires once
	 * wlr_backend_start() runs, below) - server_new_output() creates each
	 * output's own scaled copy of server.bg_buffer as outputs connect,
	 * so this just loads the shared source image once, if there is one */
	int bg_w = 0, bg_h = 0;
	const char *bg_path = config.wallpaper[0] != '\0' ? config.wallpaper : GL3_BG_IMAGE;
	uint8_t *bg_pixels = load_jpeg_xrgb(bg_path, &bg_w, &bg_h);
	if (bg_pixels != NULL) {
		server.bg_buffer = gl3_image_buffer_create(bg_pixels, bg_w, bg_h);
	} else {
		server.bg_buffer = NULL;
		/* not tied to any specific output, so one giant rect covers
		 * every output regardless of how many connect or where */
		struct wlr_scene_rect *bg = wlr_scene_rect_create(&server.scene->tree,
			1 << 15, 1 << 15, config.bg_color);
		wlr_scene_node_set_position(&bg->node, -(1 << 13), -(1 << 13));
		wlr_scene_node_lower_to_bottom(&bg->node);
	}

	/* xdg-shell is the wayland protocol for application windows */
	wl_list_init(&server.toplevels);
	server.xdg_shell = wlr_xdg_shell_create(server.wl_display, 3);
	server.new_xdg_toplevel.notify = server_new_xdg_toplevel;
	wl_signal_add(&server.xdg_shell->events.new_toplevel, &server.new_xdg_toplevel);
	server.new_xdg_popup.notify = server_new_xdg_popup;
	wl_signal_add(&server.xdg_shell->events.new_popup, &server.new_xdg_popup);

	/* tells clients gl3 handles decorations itself, so toolkits that
	 * check (most modern gtk/qt apps) skip drawing their own title bar */
	server.xdg_decoration_manager = wlr_xdg_decoration_manager_v1_create(server.wl_display);
	server.new_xdg_toplevel_decoration.notify = server_new_xdg_toplevel_decoration;
	wl_signal_add(&server.xdg_decoration_manager->events.new_toplevel_decoration,
		&server.new_xdg_toplevel_decoration);

	/* tracks the cursor image shown on screen */
	server.cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(server.cursor, server.output_layout);

	/* loads xcursor themes, keeps cursor images sharp at any scale */
	server.cursor_mgr = wlr_xcursor_manager_create(NULL, 24);

	/* wlr_cursor only draws an image, input devices drive it via events */
	server.cursor_mode = GL3_CURSOR_PASSTHROUGH;
	server.cursor_motion.notify = server_cursor_motion;
	wl_signal_add(&server.cursor->events.motion, &server.cursor_motion);
	server.cursor_motion_absolute.notify = server_cursor_motion_absolute;
	wl_signal_add(&server.cursor->events.motion_absolute,
			&server.cursor_motion_absolute);
	server.cursor_button.notify = server_cursor_button;
	wl_signal_add(&server.cursor->events.button, &server.cursor_button);
	server.cursor_axis.notify = server_cursor_axis;
	wl_signal_add(&server.cursor->events.axis, &server.cursor_axis);
	server.cursor_frame.notify = server_cursor_frame;
	wl_signal_add(&server.cursor->events.frame, &server.cursor_frame);

	/* one seat covers keyboard, pointer, touch, and tablet for this user */
	wl_list_init(&server.keyboards);
	server.new_input.notify = server_new_input;
	wl_signal_add(&server.backend->events.new_input, &server.new_input);
	server.seat = wlr_seat_create(server.wl_display, "seat0");
	server.request_cursor.notify = seat_request_cursor;
	wl_signal_add(&server.seat->events.request_set_cursor,
			&server.request_cursor);
	server.pointer_focus_change.notify = seat_pointer_focus_change;
	wl_signal_add(&server.seat->pointer_state.events.focus_change,
			&server.pointer_focus_change);
	server.request_set_selection.notify = seat_request_set_selection;
	wl_signal_add(&server.seat->events.request_set_selection,
			&server.request_set_selection);

	/* add a unix socket for clients to connect to */
	const char *socket = wl_display_add_socket_auto(server.wl_display);
	if (!socket) {
		wlr_backend_destroy(server.backend);
		return 1;
	}

	/* enumerates outputs and inputs, becomes drm master, etc */
	if (!wlr_backend_start(server.backend)) {
		wlr_backend_destroy(server.backend);
		wl_display_destroy(server.wl_display);
		return 1;
	}

	/* point clients at our socket, then run the startup command */
	setenv("WAYLAND_DISPLAY", socket, true);
	if (startup_cmd) {
		if (fork() == 0) {
			execl("/bin/sh", "/bin/sh", "-c", startup_cmd, (void *)NULL);
		}
	}

	write_help_file();
	/* first ever launch, per user, shows the keybind list automatically */
	const char *data_home = getenv("XDG_DATA_HOME");
	const char *home = getenv("HOME");
	char first_run_marker[1024] = "";
	if (data_home != NULL && data_home[0] != '\0') {
		snprintf(first_run_marker, sizeof(first_run_marker), "%s/gl3", data_home);
	} else if (home != NULL) {
		snprintf(first_run_marker, sizeof(first_run_marker), "%s/.local/share/gl3", home);
	}
	if (first_run_marker[0] != '\0') {
		mkdir_p(first_run_marker);
		char marker_file[1088];
		snprintf(marker_file, sizeof(marker_file), "%s/first-run", first_run_marker);
		if (access(marker_file, F_OK) != 0) {
			show_help();
			FILE *f = fopen(marker_file, "w");
			if (f != NULL) {
				fclose(f);
			}
		}
	}

	/* this blocks until the compositor exits */
	wlr_log(WLR_INFO, "gl3 // spiralos, pierce the heavens");
	wlr_log(WLR_INFO, "running wayland compositor on wayland_display=%s",
			socket);
	wlr_log(WLR_INFO, "press super+? for the keybind list, super+escape to quit");
	wl_display_run(server.wl_display);


	/* the loop exited, so shut everything down */
	wl_display_destroy_clients(server.wl_display);

	wl_list_remove(&server.new_xdg_toplevel.link);
	wl_list_remove(&server.new_xdg_popup.link);
	wl_list_remove(&server.new_xdg_toplevel_decoration.link);
	wl_list_remove(&server.new_layer_surface.link);

	wl_list_remove(&server.cursor_motion.link);
	wl_list_remove(&server.cursor_motion_absolute.link);
	wl_list_remove(&server.cursor_button.link);
	wl_list_remove(&server.cursor_axis.link);
	wl_list_remove(&server.cursor_frame.link);

	wl_list_remove(&server.new_input.link);
	wl_list_remove(&server.request_cursor.link);
	wl_list_remove(&server.pointer_focus_change.link);
	wl_list_remove(&server.request_set_selection.link);

	wl_list_remove(&server.new_output.link);

	wlr_scene_node_destroy(&server.scene->tree.node);
	if (server.bg_buffer != NULL) {
		wlr_buffer_drop(server.bg_buffer);
	}
	wlr_xcursor_manager_destroy(server.cursor_mgr);
	wlr_cursor_destroy(server.cursor);
	wlr_allocator_destroy(server.allocator);
	wlr_renderer_destroy(server.renderer);
	wlr_backend_destroy(server.backend);
	wl_display_destroy(server.wl_display);
	return 0;
}
