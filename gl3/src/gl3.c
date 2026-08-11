#include <assert.h>
#include <getopt.h>
#include <setjmp.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
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
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

/* gl3 config, super is the modifier key, layout is dwm-style master/stack */
#define GL3_TERMINAL "foot"
#define GL3_GAP 8
#define GL3_MASTER_RATIO 6 /* starting ratio, super+h/l adjusts it at runtime */
#define GL3_NUM_WORKSPACES 10 /* super+1-0 switches, super+shift+1-0 moves */
/* deep gurren crimson desktop background, used if the wallpaper fails to load */
static const float GL3_BG_COLOR[4] = { 0.16f, 0.02f, 0.03f, 1.0f };
/* default wallpaper, DATADIR is set by the makefile at build time */
#define GL3_BG_IMAGE GL3_DATADIR "/background.jpg"
/* drm's xrgb8888 fourcc code, matches drm_fourcc.h's DRM_FORMAT_XRGB8888 */
#define GL3_DRM_FORMAT_XRGB8888 0x34325258

/* struct members are commented where they are used, not here */
enum gl3_cursor_mode {
	GL3_CURSOR_PASSTHROUGH,
	GL3_CURSOR_MOVE,
	GL3_CURSOR_RESIZE,
};

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
	struct wl_list toplevels;

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
	struct gl3_toplevel *grabbed_toplevel;
	double grab_x, grab_y;
	struct wlr_box grab_geobox;
	uint32_t resize_edges;

	struct wlr_output_layout *output_layout;
	struct wl_list outputs;
	struct wl_listener new_output;

	int master_ratio;
	int current_workspace;
};

struct gl3_output {
	struct wl_list link;
	struct gl3_server *server;
	struct wlr_output *wlr_output;
	struct wl_listener frame;
	struct wl_listener request_state;
	struct wl_listener destroy;
};

struct gl3_toplevel {
	struct wl_list link;
	struct gl3_server *server;
	struct wlr_xdg_toplevel *xdg_toplevel;
	struct wlr_scene_tree *scene_tree;
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

struct gl3_keyboard {
	struct wl_list link;
	struct gl3_server *server;
	struct wlr_keyboard *wlr_keyboard;

	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
};

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
		}
	}
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
	/* raise it in the scene graph, tiles don't overlap so this is harmless */
	/* the toplevels list order is untouched, so tile positions stay put */
	wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
	/* activate the new surface */
	wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, true);
	/* let the seat send key events to this surface from now on */
	if (keyboard != NULL) {
		wlr_seat_keyboard_notify_enter(seat, surface,
			keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
	}
}

/* lays out windows dwm-style, first window is master, rest stack */
/* only windows on the current workspace are shown, others get hidden */
static void arrange(struct gl3_server *server) {
	struct wlr_box area = {0};
	wlr_output_layout_get_box(server->output_layout, NULL, &area);
	if (area.width == 0 || area.height == 0) {
		return;
	}

	int n = 0;
	struct gl3_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->toplevels, link) {
		bool visible = toplevel->workspace == server->current_workspace;
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, visible);
		if (visible) {
			n++;
		}
	}
	if (n == 0) {
		return;
	}

	const int gap = GL3_GAP;

	if (n == 1) {
		wl_list_for_each(toplevel, &server->toplevels, link) {
			if (toplevel->workspace != server->current_workspace) {
				continue;
			}
			wlr_scene_node_set_position(&toplevel->scene_tree->node,
				area.x + gap, area.y + gap);
			wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel,
				area.width - 2 * gap, area.height - 2 * gap);
			return;
		}
	}

	int master_w = (area.width - 3 * gap) * server->master_ratio / 10;
	int stack_w = area.width - 3 * gap - master_w;
	int stack_n = n - 1;
	/* rounding here can leave the last stack window a few px short */
	int stack_h = (area.height - (stack_n + 1) * gap) / stack_n;

	int i = 0;
	wl_list_for_each(toplevel, &server->toplevels, link) {
		if (toplevel->workspace != server->current_workspace) {
			continue;
		}
		if (i == 0) {
			wlr_scene_node_set_position(&toplevel->scene_tree->node,
				area.x + gap, area.y + gap);
			wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel,
				master_w, area.height - 2 * gap);
		} else {
			int idx = i - 1;
			wlr_scene_node_set_position(&toplevel->scene_tree->node,
				area.x + master_w + 2 * gap,
				area.y + gap + idx * (stack_h + gap));
			wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel,
				stack_w, stack_h);
		}
		i++;
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

/* moves keyboard focus to the next window on the current workspace */
static void focus_next(struct gl3_server *server) {
	struct gl3_toplevel *current = focused_toplevel(server);
	struct gl3_toplevel *first = NULL;
	struct gl3_toplevel *next = NULL;
	bool seen_current = false;
	struct gl3_toplevel *t;
	wl_list_for_each(t, &server->toplevels, link) {
		if (t->workspace != server->current_workspace) {
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

/* switches which workspace is shown, does nothing if already on it */
static void switch_workspace(struct gl3_server *server, int ws) {
	if (ws == server->current_workspace) {
		return;
	}
	server->current_workspace = ws;
	arrange(server);
	/* focus the first window on the new workspace, or clear focus */
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
	struct gl3_toplevel *toplevel = focused_toplevel(server);
	if (toplevel == NULL || toplevel->workspace == ws) {
		return;
	}
	toplevel->workspace = ws;
	arrange(server);
	/* the window just left, focus whatever's left on this workspace */
	struct gl3_toplevel *t, *replacement = NULL;
	wl_list_for_each(t, &server->toplevels, link) {
		if (t->workspace == server->current_workspace) {
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
	/* handles compositor keybinds, assumes super is already held down */
	switch (sym) {
	case XKB_KEY_Escape:
		/* super+escape - quit */
		wl_display_terminate(server->wl_display);
		break;
	case XKB_KEY_Return:
		/* super+return - spawn a terminal */
		spawn(GL3_TERMINAL);
		break;
	case XKB_KEY_q:
	case XKB_KEY_Q: {
		/* super+q - close the focused window */
		struct wlr_surface *focused =
			server->seat->keyboard_state.focused_surface;
		struct wlr_xdg_toplevel *xt = focused ?
			wlr_xdg_toplevel_try_from_wlr_surface(focused) : NULL;
		if (xt != NULL) {
			wlr_xdg_toplevel_send_close(xt);
		}
		break;
	}
	case XKB_KEY_j:
	case XKB_KEY_J:
		/* super+j - cycle focus to the next window */
		focus_next(server);
		break;
	case XKB_KEY_h:
	case XKB_KEY_H:
		/* super+h - shrink the master column */
		if (server->master_ratio > 1) {
			server->master_ratio--;
			arrange(server);
		}
		break;
	case XKB_KEY_l:
	case XKB_KEY_L:
		/* super+l - grow the master column */
		if (server->master_ratio < 9) {
			server->master_ratio++;
			arrange(server);
		}
		break;
	case XKB_KEY_1: case XKB_KEY_2: case XKB_KEY_3: case XKB_KEY_4:
	case XKB_KEY_5: case XKB_KEY_6: case XKB_KEY_7: case XKB_KEY_8:
	case XKB_KEY_9: case XKB_KEY_0: {
		/* super+1-0 - switch workspace, super+shift+1-0 - move window there */
		int ws = (sym == XKB_KEY_0) ? GL3_NUM_WORKSPACES : (int)(sym - XKB_KEY_1) + 1;
		if (shift) {
			move_focused_to_workspace(server, ws);
		} else {
			switch_workspace(server, ws);
		}
		break;
	}
	/* TODO: still no keybind to swap which window is master, or to move a
	 * window from stack into master directly, only the ratio is adjustable
	 * for now. */
	default:
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
	/* back to normal, no window grabbed */
	server->cursor_mode = GL3_CURSOR_PASSTHROUGH;
	server->grabbed_toplevel = NULL;
}

static void process_cursor_move(struct gl3_server *server) {
	/* move the grabbed window to follow the cursor */
	struct gl3_toplevel *toplevel = server->grabbed_toplevel;
	wlr_scene_node_set_position(&toplevel->scene_tree->node,
		server->cursor->x - server->grab_x,
		server->cursor->y - server->grab_y);
}

static void process_cursor_resize(struct gl3_server *server) {
	/* resize from whichever edge or corner was grabbed */
	/* takes shortcuts, doesn't wait for the client to commit the new size */
	struct gl3_toplevel *toplevel = server->grabbed_toplevel;
	double border_x = server->cursor->x - server->grab_x;
	double border_y = server->cursor->y - server->grab_y;
	int new_left = server->grab_geobox.x;
	int new_right = server->grab_geobox.x + server->grab_geobox.width;
	int new_top = server->grab_geobox.y;
	int new_bottom = server->grab_geobox.y + server->grab_geobox.height;

	if (server->resize_edges & WLR_EDGE_TOP) {
		new_top = border_y;
		if (new_top >= new_bottom) {
			new_top = new_bottom - 1;
		}
	} else if (server->resize_edges & WLR_EDGE_BOTTOM) {
		new_bottom = border_y;
		if (new_bottom <= new_top) {
			new_bottom = new_top + 1;
		}
	}
	if (server->resize_edges & WLR_EDGE_LEFT) {
		new_left = border_x;
		if (new_left >= new_right) {
			new_left = new_right - 1;
		}
	} else if (server->resize_edges & WLR_EDGE_RIGHT) {
		new_right = border_x;
		if (new_right <= new_left) {
			new_right = new_left + 1;
		}
	}

	struct wlr_box *geo_box = &toplevel->xdg_toplevel->base->geometry;
	wlr_scene_node_set_position(&toplevel->scene_tree->node,
		new_left - geo_box->x, new_top - geo_box->y);

	int new_width = new_right - new_left;
	int new_height = new_bottom - new_top;
	wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, new_width, new_height);
}

static void process_cursor_motion(struct gl3_server *server, uint32_t time) {
	/* moving or resizing a window, handle that instead */
	if (server->cursor_mode == GL3_CURSOR_MOVE) {
		process_cursor_move(server);
		return;
	} else if (server->cursor_mode == GL3_CURSOR_RESIZE) {
		process_cursor_resize(server);
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
		/* releasing ends any move or resize in progress */
		reset_cursor_mode(server);
	} else {
		/* pressed, so focus whatever is under the cursor */
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

static void output_request_state(struct wl_listener *listener, void *data) {
	/* fires when the backend wants a new output state, eg on resize */
	struct gl3_output *output = wl_container_of(listener, output, request_state);
	const struct wlr_output_event_request_state *event = data;
	wlr_output_commit_state(output->wlr_output, event->state);
}

static void output_destroy(struct wl_listener *listener, void *data) {
	struct gl3_output *output = wl_container_of(listener, output, destroy);

	wl_list_remove(&output->frame.link);
	wl_list_remove(&output->request_state.link);
	wl_list_remove(&output->destroy.link);
	wl_list_remove(&output->link);
	free(output);
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
	output->server = server;

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
}

static void xdg_toplevel_map(struct wl_listener *listener, void *data) {
	/* fires when the window is ready to show on screen */
	struct gl3_toplevel *toplevel = wl_container_of(listener, toplevel, map);

	/* new windows open on whatever workspace is visible right now */
	toplevel->workspace = toplevel->server->current_workspace;
	wl_list_insert(&toplevel->server->toplevels, &toplevel->link);

	arrange(toplevel->server);
	focus_toplevel(toplevel);
}

static void xdg_toplevel_unmap(struct wl_listener *listener, void *data) {
	/* fires when the window should no longer be shown */
	struct gl3_toplevel *toplevel = wl_container_of(listener, toplevel, unmap);

	/* if this window was being moved or resized, stop that */
	if (toplevel == toplevel->server->grabbed_toplevel) {
		reset_cursor_mode(toplevel->server);
	}

	wl_list_remove(&toplevel->link);

	arrange(toplevel->server);
}

static void xdg_toplevel_commit(struct wl_listener *listener, void *data) {
	/* fires when the client commits a new surface state */
	struct gl3_toplevel *toplevel = wl_container_of(listener, toplevel, commit);

	if (toplevel->xdg_toplevel->base->initial_commit) {
		/* first commit needs a reply, let the client pick its own size */
		wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, 0, 0);
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

static void begin_interactive(struct gl3_toplevel *toplevel,
		enum gl3_cursor_mode mode, uint32_t edges) {
	/* starts a move or resize, we grab pointer input until it's done */
	struct gl3_server *server = toplevel->server;

	server->grabbed_toplevel = toplevel;
	server->cursor_mode = mode;

	if (mode == GL3_CURSOR_MOVE) {
		server->grab_x = server->cursor->x - toplevel->scene_tree->node.x;
		server->grab_y = server->cursor->y - toplevel->scene_tree->node.y;
	} else {
		struct wlr_box *geo_box = &toplevel->xdg_toplevel->base->geometry;

		double border_x = (toplevel->scene_tree->node.x + geo_box->x) +
			((edges & WLR_EDGE_RIGHT) ? geo_box->width : 0);
		double border_y = (toplevel->scene_tree->node.y + geo_box->y) +
			((edges & WLR_EDGE_BOTTOM) ? geo_box->height : 0);
		server->grab_x = server->cursor->x - border_x;
		server->grab_y = server->cursor->y - border_y;

		server->grab_geobox = *geo_box;
		server->grab_geobox.x += toplevel->scene_tree->node.x;
		server->grab_geobox.y += toplevel->scene_tree->node.y;

		server->resize_edges = edges;
	}
}

static void xdg_toplevel_request_move(
		struct wl_listener *listener, void *data) {
	/* client asked to start a move, usually from its titlebar */
	/* TODO: doesn't check the button press serial, a client could spam this */
	struct gl3_toplevel *toplevel = wl_container_of(listener, toplevel, request_move);
	begin_interactive(toplevel, GL3_CURSOR_MOVE, 0);
}

static void xdg_toplevel_request_resize(
		struct wl_listener *listener, void *data) {
	/* client asked to start a resize, usually from its titlebar */
	/* TODO: doesn't check the button press serial, a client could spam this */
	struct wlr_xdg_toplevel_resize_event *event = data;
	struct gl3_toplevel *toplevel = wl_container_of(listener, toplevel, request_resize);
	begin_interactive(toplevel, GL3_CURSOR_RESIZE, event->edges);
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

	/* popups need adding to the scene graph under their parent */
	struct wlr_xdg_surface *parent = wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
	assert(parent != NULL);
	struct wlr_scene_tree *parent_tree = parent->data;
	xdg_popup->base->data = wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);

	popup->commit.notify = xdg_popup_commit;
	wl_signal_add(&xdg_popup->base->surface->events.commit, &popup->commit);

	popup->destroy.notify = xdg_popup_destroy;
	wl_signal_add(&xdg_popup->events.destroy, &popup->destroy);
}

int main(int argc, char *argv[]) {
	wlr_log_init(WLR_DEBUG, NULL);
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
	server.master_ratio = GL3_MASTER_RATIO;
	server.current_workspace = 1;
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

	/* wallpaper, falls back to a solid crimson color if it fails to load */
	/* TODO: doesn't scale or tile, only looks right on a single output the
	 * same size as the image, multi-monitor setups need real handling here */
	int bg_w = 0, bg_h = 0;
	uint8_t *bg_pixels = load_jpeg_xrgb(GL3_BG_IMAGE, &bg_w, &bg_h);
	if (bg_pixels != NULL) {
		struct wlr_buffer *bg_buffer = gl3_image_buffer_create(bg_pixels, bg_w, bg_h);
		struct wlr_scene_buffer *bg = wlr_scene_buffer_create(&server.scene->tree, bg_buffer);
		wlr_buffer_drop(bg_buffer);
		wlr_scene_node_set_position(&bg->node, 0, 0);
		wlr_scene_node_lower_to_bottom(&bg->node);
	} else {
		struct wlr_scene_rect *bg = wlr_scene_rect_create(&server.scene->tree,
			1 << 15, 1 << 15, GL3_BG_COLOR);
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
	/* this blocks until the compositor exits */
	wlr_log(WLR_INFO, "gl3 // spiralos - pierce the heavens!");
	wlr_log(WLR_INFO, "running wayland compositor on wayland_display=%s",
			socket);
	wlr_log(WLR_INFO, "keys: super+return - terminal, super+q - close, "
			"super+j - focus-next, super+h/l - master ratio, "
			"super+1-0 - workspace, super+shift+1-0 - move to workspace, "
			"super+escape - quit");
	wl_display_run(server.wl_display);


	/* the loop exited, so shut everything down */
	wl_display_destroy_clients(server.wl_display);

	wl_list_remove(&server.new_xdg_toplevel.link);
	wl_list_remove(&server.new_xdg_popup.link);

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
	wlr_xcursor_manager_destroy(server.cursor_mgr);
	wlr_cursor_destroy(server.cursor);
	wlr_allocator_destroy(server.allocator);
	wlr_renderer_destroy(server.renderer);
	wlr_backend_destroy(server.backend);
	wl_display_destroy(server.wl_display);
	return 0;
}
