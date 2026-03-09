#define WLR_USE_UNSTABLE
#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "nano-compositor.h"

bool nano_server_init(struct nano_server *server) {
    server->display = wl_display_create();
    if (!server->display) {
        wlr_log(WLR_ERROR, "Failed to create wl_display");
        return false;
    }

    server->backend = wlr_backend_autocreate(
        wl_display_get_event_loop(server->display), NULL);
    if (!server->backend) {
        wlr_log(WLR_ERROR, "Failed to create wlr_backend");
        wl_display_destroy(server->display);
        return false;
    }

    server->renderer = wlr_renderer_autocreate(server->backend);
    if (!server->renderer) {
        wlr_log(WLR_ERROR, "Failed to create wlr_renderer");
        wlr_backend_destroy(server->backend);
        wl_display_destroy(server->display);
        return false;
    }
    wlr_renderer_init_wl_display(server->renderer, server->display);

    server->allocator = wlr_allocator_autocreate(
        server->backend, server->renderer);
    if (!server->allocator) {
        wlr_log(WLR_ERROR, "Failed to create wlr_allocator");
        wlr_backend_destroy(server->backend);
        wl_display_destroy(server->display);
        return false;
    }

    /* Create the scene graph. All rendering is done through the scene API. */
    server->scene = wlr_scene_create();
    if (!server->scene) {
        wlr_log(WLR_ERROR, "Failed to create wlr_scene");
        return false;
    }

    /* Create scene layers from bottom to top. Order matters for z-stacking. */
    server->layer_background =
        wlr_scene_tree_create(&server->scene->tree);
    server->layer_bottom =
        wlr_scene_tree_create(&server->scene->tree);
    server->layer_toplevel =
        wlr_scene_tree_create(&server->scene->tree);
    server->layer_top =
        wlr_scene_tree_create(&server->scene->tree);
    server->layer_overlay =
        wlr_scene_tree_create(&server->scene->tree);

    /* Output layout tracks the arrangement of outputs */
    server->output_layout = wlr_output_layout_create(server->display);
    if (!server->output_layout) {
        wlr_log(WLR_ERROR, "Failed to create output layout");
        return false;
    }

    server->scene_layout = wlr_scene_attach_output_layout(
        server->scene, server->output_layout);

    /* Core Wayland protocols */
    wlr_compositor_create(server->display, 5, server->renderer);
    wlr_subcompositor_create(server->display);
    wlr_data_device_manager_create(server->display);

    /* Initialize lists */
    wl_list_init(&server->outputs);
    wl_list_init(&server->toplevels);
    wl_list_init(&server->keyboards);

    /* Initialize subsystems */
    nano_output_init(server);
    nano_input_init(server);
    nano_xdg_init(server);
    nano_layer_init(server);

    /* Set up the Wayland socket */
    const char *socket = wl_display_add_socket_auto(server->display);
    if (!socket) {
        wlr_log(WLR_ERROR, "Failed to add Wayland socket");
        wlr_backend_destroy(server->backend);
        wl_display_destroy(server->display);
        return false;
    }

    if (!wlr_backend_start(server->backend)) {
        wlr_log(WLR_ERROR, "Failed to start backend");
        wlr_backend_destroy(server->backend);
        wl_display_destroy(server->display);
        return false;
    }

    setenv("WAYLAND_DISPLAY", socket, true);
    wlr_log(WLR_INFO,
        "nano-compositor running on WAYLAND_DISPLAY=%s", socket);

    return true;
}

void nano_server_run(struct nano_server *server) {
    wlr_log(WLR_INFO, "Entering event loop");
    wl_display_run(server->display);
}

void nano_server_destroy(struct nano_server *server) {
    wlr_log(WLR_INFO, "Shutting down nano-compositor");
    wl_display_destroy_clients(server->display);
    wlr_scene_node_destroy(&server->scene->tree.node);
    wlr_xcursor_manager_destroy(server->cursor_mgr);
    wlr_cursor_destroy(server->cursor);
    wl_display_destroy(server->display);
}

void nano_focus_toplevel(struct nano_toplevel *toplevel) {
    if (!toplevel) {
        return;
    }

    struct nano_server *server = toplevel->server;
    struct wlr_seat *seat = server->seat;
    struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;
    struct wlr_surface *surface =
        toplevel->xdg_toplevel->base->surface;

    if (prev_surface == surface) {
        /* Already focused */
        return;
    }

    if (prev_surface) {
        /*
         * Deactivate the previously focused surface. This lets the client
         * know it no longer has focus and can e.g. stop displaying a caret.
         */
        struct wlr_xdg_toplevel *prev_toplevel =
            wlr_xdg_toplevel_try_from_wlr_surface(prev_surface);
        if (prev_toplevel) {
            wlr_xdg_toplevel_set_activated(prev_toplevel, false);
        }
    }

    /* Bring the scene tree node to the front */
    wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);

    /* Move the toplevel to the front of the list */
    wl_list_remove(&toplevel->link);
    wl_list_insert(&server->toplevels, &toplevel->link);

    /* Activate the new surface */
    wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, true);

    /* Send keyboard focus */
    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
    if (keyboard) {
        wlr_seat_keyboard_notify_enter(seat, surface,
            keyboard->keycodes, keyboard->num_keycodes,
            &keyboard->modifiers);
    }
}

struct nano_toplevel *nano_toplevel_at(struct nano_server *server,
        double lx, double ly, struct wlr_surface **surface,
        double *sx, double *sy) {
    struct wlr_scene_node *node =
        wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);
    if (!node || node->type != WLR_SCENE_NODE_BUFFER) {
        return NULL;
    }

    struct wlr_scene_buffer *scene_buffer =
        wlr_scene_buffer_from_node(node);
    struct wlr_scene_surface *scene_surface =
        wlr_scene_surface_try_from_buffer(scene_buffer);
    if (!scene_surface) {
        return NULL;
    }

    *surface = scene_surface->surface;

    /*
     * Walk up the scene tree to find the nano_toplevel. The toplevel's
     * scene_tree has data pointing to itself.
     */
    struct wlr_scene_tree *tree = node->parent;
    while (tree && !tree->node.data) {
        tree = tree->node.parent;
    }

    if (!tree || !tree->node.data) {
        return NULL;
    }

    return tree->node.data;
}
