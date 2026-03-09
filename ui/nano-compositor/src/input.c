#define WLR_USE_UNSTABLE
#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <xkbcommon/xkbcommon.h>

#include "nano-compositor.h"

/*
 * Handle compositor keybindings.
 * Returns true if the key was consumed by the compositor.
 */
static bool handle_keybinding(struct nano_server *server, xkb_keysym_t sym) {
    switch (sym) {
    case XKB_KEY_Escape:
        /* Alt+Escape: terminate the compositor */
        wlr_log(WLR_INFO, "Alt+Escape pressed, exiting compositor");
        wl_display_terminate(server->display);
        return true;

    case XKB_KEY_Tab: {
        /* Alt+Tab: cycle to the next toplevel */
        if (wl_list_length(&server->toplevels) < 2) {
            return true;
        }
        struct nano_toplevel *next = wl_container_of(
            server->toplevels.prev, next, link);
        nano_focus_toplevel(next);
        return true;
    }

    default:
        return false;
    }
}

static void keyboard_handle_key(struct wl_listener *listener, void *data) {
    struct nano_keyboard *keyboard =
        wl_container_of(listener, keyboard, key);
    struct nano_server *server = keyboard->server;
    struct wlr_keyboard_key_event *event = data;

    /* Translate libinput keycode to XKB keycode */
    uint32_t keycode = event->keycode + 8;

    const xkb_keysym_t *syms;
    int nsyms = xkb_state_key_get_syms(
        keyboard->wlr_keyboard->xkb_state, keycode, &syms);

    bool handled = false;
    uint32_t modifiers =
        wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);

    if ((modifiers & WLR_MODIFIER_ALT) &&
            event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        /* Alt is held and a key was pressed; check for compositor bindings */
        for (int i = 0; i < nsyms; i++) {
            handled = handle_keybinding(server, syms[i]);
            if (handled) {
                break;
            }
        }
    }

    if (!handled) {
        /* Forward the key event to the focused client */
        wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);
        wlr_seat_keyboard_notify_key(server->seat,
            event->time_msec, event->keycode, event->state);
    }
}

static void keyboard_handle_modifiers(struct wl_listener *listener,
        void *data) {
    struct nano_keyboard *keyboard =
        wl_container_of(listener, keyboard, modifiers);

    wlr_seat_set_keyboard(keyboard->server->seat, keyboard->wlr_keyboard);
    wlr_seat_keyboard_notify_modifiers(keyboard->server->seat,
        &keyboard->wlr_keyboard->modifiers);
}

static void keyboard_handle_destroy(struct wl_listener *listener, void *data) {
    struct nano_keyboard *keyboard =
        wl_container_of(listener, keyboard, destroy);

    wl_list_remove(&keyboard->modifiers.link);
    wl_list_remove(&keyboard->key.link);
    wl_list_remove(&keyboard->destroy.link);
    wl_list_remove(&keyboard->link);

    free(keyboard);
}

static void server_new_keyboard(struct nano_server *server,
        struct wlr_input_device *device) {
    struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);

    struct nano_keyboard *keyboard = calloc(1, sizeof(*keyboard));
    if (!keyboard) {
        wlr_log(WLR_ERROR, "Failed to allocate nano_keyboard");
        return;
    }

    keyboard->server = server;
    keyboard->wlr_keyboard = wlr_keyboard;

    /* Set up XKB keymap (default layout) */
    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!context) {
        wlr_log(WLR_ERROR, "Failed to create XKB context");
        free(keyboard);
        return;
    }

    struct xkb_keymap *keymap = xkb_keymap_new_from_names(
        context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!keymap) {
        wlr_log(WLR_ERROR, "Failed to create XKB keymap");
        xkb_context_unref(context);
        free(keyboard);
        return;
    }

    wlr_keyboard_set_keymap(wlr_keyboard, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);

    wlr_keyboard_set_repeat_info(wlr_keyboard, 25, 600);

    /* Register listeners */
    keyboard->modifiers.notify = keyboard_handle_modifiers;
    wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);

    keyboard->key.notify = keyboard_handle_key;
    wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);

    keyboard->destroy.notify = keyboard_handle_destroy;
    wl_signal_add(&device->events.destroy, &keyboard->destroy);

    wlr_seat_set_keyboard(server->seat, wlr_keyboard);
    wl_list_insert(&server->keyboards, &keyboard->link);
}

static void server_new_pointer(struct nano_server *server,
        struct wlr_input_device *device) {
    /*
     * Attach the pointer device to the cursor so it can drive cursor
     * movement.
     */
    wlr_cursor_attach_input_device(server->cursor, device);
}

static void server_new_touch(struct nano_server *server,
        struct wlr_input_device *device) {
    /* Attach the touch device to the cursor */
    wlr_cursor_attach_input_device(server->cursor, device);
}

/* --- Cursor event handlers --- */

void nano_process_cursor_motion(struct nano_server *server, uint32_t time) {
    if (server->cursor_mode == NANO_CURSOR_MOVE) {
        /* Interactive move */
        struct nano_toplevel *toplevel = server->grabbed_toplevel;
        if (toplevel) {
            wlr_scene_node_set_position(&toplevel->scene_tree->node,
                server->cursor->x - server->grab_x,
                server->cursor->y - server->grab_y);
        }
        return;
    } else if (server->cursor_mode == NANO_CURSOR_RESIZE) {
        /* Interactive resize */
        struct nano_toplevel *toplevel = server->grabbed_toplevel;
        if (!toplevel) {
            return;
        }

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

        struct wlr_box geo_box;
        wlr_xdg_surface_get_geometry(
            toplevel->xdg_toplevel->base, &geo_box);
        wlr_scene_node_set_position(&toplevel->scene_tree->node,
            new_left - geo_box.x, new_top - geo_box.y);

        int new_width = new_right - new_left;
        int new_height = new_bottom - new_top;
        wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel,
            new_width, new_height);
        return;
    }

    /* Passthrough mode: find the surface under cursor and notify the seat */
    double sx, sy;
    struct wlr_surface *surface = NULL;
    struct nano_toplevel *toplevel =
        nano_toplevel_at(server, server->cursor->x, server->cursor->y,
            &surface, &sx, &sy);

    if (!toplevel) {
        /* If no surface, set the default cursor image */
        wlr_cursor_set_xcursor(server->cursor,
            server->cursor_mgr, "default");
    }

    if (surface) {
        wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
        wlr_seat_pointer_notify_motion(server->seat, time, sx, sy);
    } else {
        wlr_seat_pointer_clear_focus(server->seat);
    }
}

static void cursor_motion(struct wl_listener *listener, void *data) {
    struct nano_server *server =
        wl_container_of(listener, server, cursor_motion);
    struct wlr_pointer_motion_event *event = data;

    wlr_cursor_move(server->cursor, &event->pointer->base,
        event->delta_x, event->delta_y);
    nano_process_cursor_motion(server, event->time_msec);
}

static void cursor_motion_absolute(struct wl_listener *listener, void *data) {
    struct nano_server *server =
        wl_container_of(listener, server, cursor_motion_absolute);
    struct wlr_pointer_motion_absolute_event *event = data;

    wlr_cursor_warp_absolute(server->cursor, &event->pointer->base,
        event->x, event->y);
    nano_process_cursor_motion(server, event->time_msec);
}

static void cursor_button(struct wl_listener *listener, void *data) {
    struct nano_server *server =
        wl_container_of(listener, server, cursor_button);
    struct wlr_pointer_button_event *event = data;

    wlr_seat_pointer_notify_button(server->seat,
        event->time_msec, event->button, event->state);

    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
        /* End any interactive move/resize */
        if (server->cursor_mode != NANO_CURSOR_PASSTHROUGH) {
            server->cursor_mode = NANO_CURSOR_PASSTHROUGH;
            server->grabbed_toplevel = NULL;
        }
    } else {
        /* Focus the surface under the cursor on press */
        double sx, sy;
        struct wlr_surface *surface = NULL;
        struct nano_toplevel *toplevel =
            nano_toplevel_at(server, server->cursor->x, server->cursor->y,
                &surface, &sx, &sy);
        if (toplevel) {
            nano_focus_toplevel(toplevel);
        }
    }
}

static void cursor_axis(struct wl_listener *listener, void *data) {
    struct nano_server *server =
        wl_container_of(listener, server, cursor_axis);
    struct wlr_pointer_axis_event *event = data;

    wlr_seat_pointer_notify_axis(server->seat,
        event->time_msec, event->orientation, event->delta,
        event->delta_discrete, event->source, event->relative_direction);
}

static void cursor_frame(struct wl_listener *listener, void *data) {
    struct nano_server *server =
        wl_container_of(listener, server, cursor_frame);

    wlr_seat_pointer_notify_frame(server->seat);
}

/* --- Touch event handlers --- */

static void cursor_touch_down(struct wl_listener *listener, void *data) {
    struct nano_server *server =
        wl_container_of(listener, server, cursor_touch_down);
    struct wlr_touch_down_event *event = data;

    double lx, ly;
    wlr_cursor_absolute_to_layout_coords(server->cursor,
        &event->touch->base, event->x, event->y, &lx, &ly);

    double sx, sy;
    struct wlr_surface *surface = NULL;
    struct nano_toplevel *toplevel =
        nano_toplevel_at(server, lx, ly, &surface, &sx, &sy);

    if (toplevel) {
        nano_focus_toplevel(toplevel);
    }

    if (surface) {
        wlr_seat_touch_notify_down(server->seat, surface,
            event->time_msec, event->touch_id, sx, sy);
    }
}

static void cursor_touch_up(struct wl_listener *listener, void *data) {
    struct nano_server *server =
        wl_container_of(listener, server, cursor_touch_up);
    struct wlr_touch_up_event *event = data;

    wlr_seat_touch_notify_up(server->seat,
        event->time_msec, event->touch_id);
}

static void cursor_touch_motion(struct wl_listener *listener, void *data) {
    struct nano_server *server =
        wl_container_of(listener, server, cursor_touch_motion);
    struct wlr_touch_motion_event *event = data;

    double lx, ly;
    wlr_cursor_absolute_to_layout_coords(server->cursor,
        &event->touch->base, event->x, event->y, &lx, &ly);

    double sx, sy;
    struct wlr_surface *surface = NULL;
    nano_toplevel_at(server, lx, ly, &surface, &sx, &sy);

    if (surface) {
        wlr_seat_touch_notify_motion(server->seat,
            event->time_msec, event->touch_id, sx, sy);
    }
}

/* --- Seat request handlers --- */

static void seat_request_cursor(struct wl_listener *listener, void *data) {
    struct nano_server *server =
        wl_container_of(listener, server, request_cursor);
    struct wlr_seat_pointer_request_set_cursor_event *event = data;
    struct wlr_seat_client *focused_client =
        server->seat->pointer_state.focused_client;

    /* Only honor cursor requests from the focused client */
    if (focused_client == event->seat_client) {
        wlr_cursor_set_surface(server->cursor, event->surface,
            event->hotspot_x, event->hotspot_y);
    }
}

static void seat_request_set_selection(struct wl_listener *listener,
        void *data) {
    struct nano_server *server =
        wl_container_of(listener, server, request_set_selection);
    struct wlr_seat_request_set_selection_event *event = data;

    wlr_seat_set_selection(server->seat, event->source, event->serial);
}

/* --- New input device --- */

static void server_new_input(struct wl_listener *listener, void *data) {
    struct nano_server *server =
        wl_container_of(listener, server, new_input);
    struct wlr_input_device *device = data;

    switch (device->type) {
    case WLR_INPUT_DEVICE_KEYBOARD:
        server_new_keyboard(server, device);
        break;
    case WLR_INPUT_DEVICE_POINTER:
        server_new_pointer(server, device);
        break;
    case WLR_INPUT_DEVICE_TOUCH:
        server_new_touch(server, device);
        break;
    default:
        break;
    }

    /* Advertise the seat capabilities based on connected devices */
    uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
    if (!wl_list_empty(&server->keyboards)) {
        caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    }
    caps |= WL_SEAT_CAPABILITY_TOUCH;
    wlr_seat_set_capabilities(server->seat, caps);
}

void nano_input_init(struct nano_server *server) {
    /* Create the cursor and xcursor manager */
    server->cursor = wlr_cursor_create();
    if (!server->cursor) {
        wlr_log(WLR_ERROR, "Failed to create cursor");
        return;
    }
    wlr_cursor_attach_output_layout(server->cursor, server->output_layout);

    server->cursor_mgr = wlr_xcursor_manager_create(NULL, 24);
    if (!server->cursor_mgr) {
        wlr_log(WLR_ERROR, "Failed to create xcursor manager");
    }

    server->cursor_mode = NANO_CURSOR_PASSTHROUGH;

    /* Cursor motion events */
    server->cursor_motion.notify = cursor_motion;
    wl_signal_add(&server->cursor->events.motion, &server->cursor_motion);

    server->cursor_motion_absolute.notify = cursor_motion_absolute;
    wl_signal_add(&server->cursor->events.motion_absolute,
        &server->cursor_motion_absolute);

    server->cursor_button.notify = cursor_button;
    wl_signal_add(&server->cursor->events.button, &server->cursor_button);

    server->cursor_axis.notify = cursor_axis;
    wl_signal_add(&server->cursor->events.axis, &server->cursor_axis);

    server->cursor_frame.notify = cursor_frame;
    wl_signal_add(&server->cursor->events.frame, &server->cursor_frame);

    /* Touch events */
    server->cursor_touch_down.notify = cursor_touch_down;
    wl_signal_add(&server->cursor->events.touch_down,
        &server->cursor_touch_down);

    server->cursor_touch_up.notify = cursor_touch_up;
    wl_signal_add(&server->cursor->events.touch_up,
        &server->cursor_touch_up);

    server->cursor_touch_motion.notify = cursor_touch_motion;
    wl_signal_add(&server->cursor->events.touch_motion,
        &server->cursor_touch_motion);

    /* Create the seat */
    server->seat = wlr_seat_create(server->display, "seat0");
    if (!server->seat) {
        wlr_log(WLR_ERROR, "Failed to create seat");
        return;
    }

    server->request_cursor.notify = seat_request_cursor;
    wl_signal_add(&server->seat->events.request_set_cursor,
        &server->request_cursor);

    server->request_set_selection.notify = seat_request_set_selection;
    wl_signal_add(&server->seat->events.request_set_selection,
        &server->request_set_selection);

    /* New input device */
    server->new_input.notify = server_new_input;
    wl_signal_add(&server->backend->events.new_input, &server->new_input);
}
