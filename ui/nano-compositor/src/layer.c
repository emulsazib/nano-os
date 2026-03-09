#define WLR_USE_UNSTABLE
#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <string.h>

#include "nano-compositor.h"

static struct wlr_scene_tree *layer_get_scene_tree(
        struct nano_server *server,
        enum zwlr_layer_shell_v1_layer layer_type) {
    switch (layer_type) {
    case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND:
        return server->layer_background;
    case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:
        return server->layer_bottom;
    case ZWLR_LAYER_SHELL_V1_LAYER_TOP:
        return server->layer_top;
    case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:
        return server->layer_overlay;
    default:
        return server->layer_top;
    }
}

/*
 * Arrange all layer surfaces for a given output. This calculates the
 * exclusive zones and positions each layer surface according to its
 * anchor and margin settings.
 */
void nano_arrange_layers(struct nano_output *output) {
    struct nano_server *server = output->server;
    int width, height;
    wlr_output_effective_resolution(output->wlr_output, &width, &height);

    struct wlr_box full_area = {0, 0, width, height};
    struct wlr_box usable_area = {0, 0, width, height};

    /*
     * Arrange layer surfaces in priority order. Surfaces with exclusive
     * zones (like panels and status bars) reduce the usable area.
     * We process all layers, and wlr_scene_layer_surface_v1_configure
     * handles exclusive zone arithmetic for us.
     *
     * Order: overlay first (highest priority for exclusive zones),
     * then top, bottom, background.
     */
    struct wlr_scene_tree *layer_trees[] = {
        server->layer_overlay,
        server->layer_top,
        server->layer_bottom,
        server->layer_background,
    };

    for (size_t i = 0; i < sizeof(layer_trees) / sizeof(layer_trees[0]); i++) {
        struct wlr_scene_node *node;
        wl_list_for_each(node, &layer_trees[i]->children, link) {
            struct nano_layer_surface *layer_data = node->data;
            if (!layer_data || !layer_data->scene_layer) {
                continue;
            }

            wlr_scene_layer_surface_v1_configure(
                layer_data->scene_layer, &full_area, &usable_area);
        }
    }
}

/* --- Layer surface event handlers --- */

static void layer_surface_map(struct wl_listener *listener, void *data) {
    struct nano_layer_surface *layer =
        wl_container_of(listener, layer, map);

    wlr_log(WLR_DEBUG, "Layer surface mapped: namespace=%s",
        layer->layer_surface->namespace);

    /* Re-arrange layers when a surface maps */
    struct nano_output *output;
    wl_list_for_each(output, &layer->server->outputs, link) {
        nano_arrange_layers(output);
        break; /* Mobile: single output */
    }

    /* If the layer surface wants keyboard interactivity, grant focus */
    if (layer->layer_surface->current.keyboard_interactive &&
            layer->layer_surface->surface->mapped) {
        struct wlr_seat *seat = layer->server->seat;
        struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
        if (keyboard) {
            wlr_seat_keyboard_notify_enter(seat,
                layer->layer_surface->surface,
                keyboard->keycodes, keyboard->num_keycodes,
                &keyboard->modifiers);
        }
    }
}

static void layer_surface_unmap(struct wl_listener *listener, void *data) {
    struct nano_layer_surface *layer =
        wl_container_of(listener, layer, unmap);

    wlr_log(WLR_DEBUG, "Layer surface unmapped: namespace=%s",
        layer->layer_surface->namespace);

    /* Re-arrange layers */
    struct nano_output *output;
    wl_list_for_each(output, &layer->server->outputs, link) {
        nano_arrange_layers(output);
        break;
    }
}

static void layer_surface_commit(struct wl_listener *listener, void *data) {
    struct nano_layer_surface *layer =
        wl_container_of(listener, layer, commit);

    if (!layer->layer_surface->initialized) {
        return;
    }

    /* Re-arrange on every commit to handle geometry/anchor changes */
    struct nano_output *output;
    wl_list_for_each(output, &layer->server->outputs, link) {
        nano_arrange_layers(output);
        break;
    }
}

static void layer_surface_destroy(struct wl_listener *listener, void *data) {
    struct nano_layer_surface *layer =
        wl_container_of(listener, layer, destroy);

    wlr_log(WLR_DEBUG, "Layer surface destroyed: namespace=%s",
        layer->layer_surface->namespace);

    wl_list_remove(&layer->map.link);
    wl_list_remove(&layer->unmap.link);
    wl_list_remove(&layer->commit.link);
    wl_list_remove(&layer->destroy.link);
    wl_list_remove(&layer->new_popup.link);

    free(layer);
}

/* --- Layer surface popup handling --- */

static void layer_popup_commit(struct wl_listener *listener, void *data) {
    struct nano_popup *popup = wl_container_of(listener, popup, commit);

    if (popup->xdg_popup->base->initial_commit) {
        wlr_xdg_popup_unconstrain_from_box(popup->xdg_popup,
            &(struct wlr_box){
                .x = -popup->xdg_popup->current.geometry.x,
                .y = -popup->xdg_popup->current.geometry.y,
                .width = INT32_MAX,
                .height = INT32_MAX,
            });
    }
}

static void layer_popup_destroy(struct wl_listener *listener, void *data) {
    struct nano_popup *popup = wl_container_of(listener, popup, destroy);

    wl_list_remove(&popup->commit.link);
    wl_list_remove(&popup->destroy.link);

    free(popup);
}

static void layer_surface_new_popup(struct wl_listener *listener, void *data) {
    struct nano_layer_surface *layer =
        wl_container_of(listener, layer, new_popup);
    struct wlr_xdg_popup *xdg_popup = data;

    struct nano_popup *popup = calloc(1, sizeof(*popup));
    if (!popup) {
        wlr_log(WLR_ERROR, "Failed to allocate layer popup");
        return;
    }

    popup->xdg_popup = xdg_popup;

    struct wlr_xdg_surface *parent =
        wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
    struct wlr_scene_tree *parent_tree = parent ? parent->data : NULL;

    if (!parent_tree) {
        /* Fallback to the layer surface's scene tree */
        parent_tree = &layer->scene_layer->tree->node.parent
            ? layer->scene_layer->tree : NULL;
        if (!parent_tree) {
            wlr_log(WLR_ERROR, "No parent tree for layer popup");
            free(popup);
            return;
        }
    }

    popup->scene_tree = wlr_scene_xdg_surface_create(
        parent_tree, xdg_popup->base);
    if (!popup->scene_tree) {
        free(popup);
        return;
    }

    xdg_popup->base->data = popup->scene_tree;

    popup->commit.notify = layer_popup_commit;
    wl_signal_add(&xdg_popup->base->surface->events.commit, &popup->commit);

    popup->destroy.notify = layer_popup_destroy;
    wl_signal_add(&xdg_popup->events.destroy, &popup->destroy);
}

/* --- New layer surface --- */

static void server_new_layer_surface(struct wl_listener *listener,
        void *data) {
    struct nano_server *server =
        wl_container_of(listener, server, new_layer_surface);
    struct wlr_layer_surface_v1 *layer_surface = data;

    wlr_log(WLR_DEBUG, "New layer surface: namespace=%s, layer=%d",
        layer_surface->namespace,
        layer_surface->pending.layer);

    /*
     * If the client didn't specify an output, assign the first one.
     * On mobile there's typically only one output.
     */
    if (!layer_surface->output) {
        struct nano_output *output;
        wl_list_for_each(output, &server->outputs, link) {
            layer_surface->output = output->wlr_output;
            break;
        }
        if (!layer_surface->output) {
            wlr_log(WLR_ERROR, "No output available for layer surface");
            wlr_layer_surface_v1_destroy(layer_surface);
            return;
        }
    }

    struct nano_layer_surface *layer = calloc(1, sizeof(*layer));
    if (!layer) {
        wlr_log(WLR_ERROR, "Failed to allocate nano_layer_surface");
        return;
    }

    layer->server = server;
    layer->layer_surface = layer_surface;

    /* Determine which scene layer to place this surface in */
    struct wlr_scene_tree *parent_tree =
        layer_get_scene_tree(server, layer_surface->pending.layer);

    /* Create the scene layer surface */
    layer->scene_layer = wlr_scene_layer_surface_v1_create(
        parent_tree, layer_surface);
    if (!layer->scene_layer) {
        wlr_log(WLR_ERROR, "Failed to create scene layer surface");
        free(layer);
        return;
    }

    /* Store data pointer for arrangement and hit-testing */
    layer->scene_layer->tree->node.data = layer;
    layer_surface->data = layer;

    /* Register listeners */
    layer->map.notify = layer_surface_map;
    wl_signal_add(&layer_surface->surface->events.map, &layer->map);

    layer->unmap.notify = layer_surface_unmap;
    wl_signal_add(&layer_surface->surface->events.unmap, &layer->unmap);

    layer->commit.notify = layer_surface_commit;
    wl_signal_add(&layer_surface->surface->events.commit, &layer->commit);

    layer->destroy.notify = layer_surface_destroy;
    wl_signal_add(&layer_surface->events.destroy, &layer->destroy);

    layer->new_popup.notify = layer_surface_new_popup;
    wl_signal_add(&layer_surface->events.new_popup, &layer->new_popup);

    /* Do initial arrangement */
    struct nano_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        nano_arrange_layers(output);
        break;
    }
}

void nano_layer_init(struct nano_server *server) {
    server->layer_shell = wlr_layer_shell_v1_create(server->display, 4);
    if (!server->layer_shell) {
        wlr_log(WLR_ERROR, "Failed to create layer shell");
        return;
    }

    server->new_layer_surface.notify = server_new_layer_surface;
    wl_signal_add(&server->layer_shell->events.new_surface,
        &server->new_layer_surface);
}
