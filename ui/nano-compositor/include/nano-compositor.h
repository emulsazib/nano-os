#ifndef NANO_COMPOSITOR_H
#define NANO_COMPOSITOR_H

#define _POSIX_C_SOURCE 200809L

#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
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
#include <wlr/types/wlr_touch.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

enum nano_cursor_mode {
    NANO_CURSOR_PASSTHROUGH,
    NANO_CURSOR_MOVE,
    NANO_CURSOR_RESIZE,
};

struct nano_server {
    struct wl_display *display;
    struct wlr_backend *backend;
    struct wlr_renderer *renderer;
    struct wlr_allocator *allocator;

    struct wlr_scene *scene;
    struct wlr_scene_output_layout *scene_layout;

    struct wlr_output_layout *output_layout;
    struct wl_list outputs; /* nano_output.link */

    struct wlr_xdg_shell *xdg_shell;
    struct wl_list toplevels; /* nano_toplevel.link */

    struct wlr_layer_shell_v1 *layer_shell;

    struct wlr_cursor *cursor;
    struct wlr_xcursor_manager *cursor_mgr;
    enum nano_cursor_mode cursor_mode;
    struct nano_toplevel *grabbed_toplevel;
    double grab_x, grab_y;
    struct wlr_box grab_geobox;
    uint32_t resize_edges;

    struct wlr_seat *seat;
    struct wl_list keyboards; /* nano_keyboard.link */

    /* Scene tree layers (bottom to top) */
    struct wlr_scene_tree *layer_background;
    struct wlr_scene_tree *layer_bottom;
    struct wlr_scene_tree *layer_toplevel;
    struct wlr_scene_tree *layer_top;
    struct wlr_scene_tree *layer_overlay;

    struct wl_listener new_output;
    struct wl_listener new_xdg_toplevel;
    struct wl_listener new_xdg_popup;
    struct wl_listener new_layer_surface;
    struct wl_listener new_input;
    struct wl_listener cursor_motion;
    struct wl_listener cursor_motion_absolute;
    struct wl_listener cursor_button;
    struct wl_listener cursor_axis;
    struct wl_listener cursor_frame;
    struct wl_listener cursor_touch_down;
    struct wl_listener cursor_touch_up;
    struct wl_listener cursor_touch_motion;
    struct wl_listener request_cursor;
    struct wl_listener request_set_selection;
};

struct nano_output {
    struct wl_list link;
    struct nano_server *server;
    struct wlr_output *wlr_output;
    struct wlr_scene_output *scene_output;

    struct wl_listener frame;
    struct wl_listener request_state;
    struct wl_listener destroy;
};

struct nano_toplevel {
    struct wl_list link;
    struct nano_server *server;
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
};

struct nano_popup {
    struct wlr_xdg_popup *xdg_popup;
    struct wlr_scene_tree *scene_tree;
    struct wl_listener commit;
    struct wl_listener destroy;
};

struct nano_layer_surface {
    struct nano_server *server;
    struct wlr_layer_surface_v1 *layer_surface;
    struct wlr_scene_layer_surface_v1 *scene_layer;

    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener destroy;
    struct wl_listener new_popup;
};

struct nano_keyboard {
    struct wl_list link;
    struct nano_server *server;
    struct wlr_keyboard *wlr_keyboard;

    struct wl_listener modifiers;
    struct wl_listener key;
    struct wl_listener destroy;
};

/* server.c */
bool nano_server_init(struct nano_server *server);
void nano_server_run(struct nano_server *server);
void nano_server_destroy(struct nano_server *server);
void nano_focus_toplevel(struct nano_toplevel *toplevel);
struct nano_toplevel *nano_toplevel_at(struct nano_server *server,
    double lx, double ly, struct wlr_surface **surface,
    double *sx, double *sy);

/* output.c */
void nano_output_init(struct nano_server *server);

/* input.c */
void nano_input_init(struct nano_server *server);
void nano_process_cursor_motion(struct nano_server *server, uint32_t time);

/* xdg.c */
void nano_xdg_init(struct nano_server *server);

/* layer.c */
void nano_layer_init(struct nano_server *server);
void nano_arrange_layers(struct nano_output *output);

#endif
