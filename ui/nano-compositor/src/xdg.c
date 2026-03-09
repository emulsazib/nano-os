#define WLR_USE_UNSTABLE
#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>

#include "nano-compositor.h"

/*
 * Auto-maximize a toplevel to fill the entire output.
 * This is the mobile-first behavior: all windows are fullscreen by default.
 */
static void auto_maximize_toplevel(struct nano_toplevel *toplevel) {
    struct nano_server *server = toplevel->server;

    /* Find the first output (mobile device typically has one) */
    struct nano_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        int width, height;
        wlr_output_effective_resolution(output->wlr_output, &width, &height);

        wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel, true);
        wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, width, height);

        /* Position at origin */
        wlr_scene_node_set_position(&toplevel->scene_tree->node, 0, 0);
        break;
    }
}

static void xdg_toplevel_map(struct wl_listener *listener, void *data) {
    struct nano_toplevel *toplevel =
        wl_container_of(listener, toplevel, map);

    wl_list_insert(&toplevel->server->toplevels, &toplevel->link);

    /* Mobile behavior: auto-maximize new windows */
    auto_maximize_toplevel(toplevel);

    nano_focus_toplevel(toplevel);
}

static void xdg_toplevel_unmap(struct wl_listener *listener, void *data) {
    struct nano_toplevel *toplevel =
        wl_container_of(listener, toplevel, unmap);

    /* If this was the grabbed toplevel, reset cursor mode */
    if (toplevel == toplevel->server->grabbed_toplevel) {
        toplevel->server->cursor_mode = NANO_CURSOR_PASSTHROUGH;
        toplevel->server->grabbed_toplevel = NULL;
    }

    wl_list_remove(&toplevel->link);

    /* Focus the next toplevel if any */
    if (!wl_list_empty(&toplevel->server->toplevels)) {
        struct nano_toplevel *next = wl_container_of(
            toplevel->server->toplevels.next, next, link);
        nano_focus_toplevel(next);
    }
}

static void xdg_toplevel_commit(struct wl_listener *listener, void *data) {
    struct nano_toplevel *toplevel =
        wl_container_of(listener, toplevel, commit);

    if (toplevel->xdg_toplevel->base->initial_commit) {
        /*
         * On initial commit, set the toplevel to maximized size before
         * the client has a chance to draw anything.
         */
        auto_maximize_toplevel(toplevel);
    }
}

static void xdg_toplevel_destroy(struct wl_listener *listener, void *data) {
    struct nano_toplevel *toplevel =
        wl_container_of(listener, toplevel, destroy);

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

static void begin_interactive_move(struct nano_toplevel *toplevel) {
    struct nano_server *server = toplevel->server;

    /* Don't allow move if a different surface is focused */
    struct wlr_surface *focused =
        server->seat->pointer_state.focused_surface;
    if (toplevel->xdg_toplevel->base->surface !=
            wlr_surface_get_root_surface(focused)) {
        return;
    }

    server->grabbed_toplevel = toplevel;
    server->cursor_mode = NANO_CURSOR_MOVE;
    server->grab_x = server->cursor->x -
        toplevel->scene_tree->node.x;
    server->grab_y = server->cursor->y -
        toplevel->scene_tree->node.y;
}

static void begin_interactive_resize(struct nano_toplevel *toplevel,
        uint32_t edges) {
    struct nano_server *server = toplevel->server;

    struct wlr_surface *focused =
        server->seat->pointer_state.focused_surface;
    if (toplevel->xdg_toplevel->base->surface !=
            wlr_surface_get_root_surface(focused)) {
        return;
    }

    struct wlr_box geo_box;
    wlr_xdg_surface_get_geometry(toplevel->xdg_toplevel->base, &geo_box);

    double border_x = (toplevel->scene_tree->node.x + geo_box.x) +
        ((edges & WLR_EDGE_RIGHT) ? geo_box.width : 0);
    double border_y = (toplevel->scene_tree->node.y + geo_box.y) +
        ((edges & WLR_EDGE_BOTTOM) ? geo_box.height : 0);

    server->grabbed_toplevel = toplevel;
    server->cursor_mode = NANO_CURSOR_RESIZE;
    server->resize_edges = edges;
    server->grab_x = server->cursor->x - border_x;
    server->grab_y = server->cursor->y - border_y;
    server->grab_geobox = geo_box;
    server->grab_geobox.x += toplevel->scene_tree->node.x;
    server->grab_geobox.y += toplevel->scene_tree->node.y;
}

static void xdg_toplevel_request_move(struct wl_listener *listener,
        void *data) {
    struct nano_toplevel *toplevel =
        wl_container_of(listener, toplevel, request_move);
    begin_interactive_move(toplevel);
}

static void xdg_toplevel_request_resize(struct wl_listener *listener,
        void *data) {
    struct nano_toplevel *toplevel =
        wl_container_of(listener, toplevel, request_resize);
    struct wlr_xdg_toplevel_resize_event *event = data;
    begin_interactive_resize(toplevel, event->edges);
}

static void xdg_toplevel_request_maximize(struct wl_listener *listener,
        void *data) {
    struct nano_toplevel *toplevel =
        wl_container_of(listener, toplevel, request_maximize);
    /* Mobile mode: always maximize */
    auto_maximize_toplevel(toplevel);
}

static void xdg_toplevel_request_fullscreen(struct wl_listener *listener,
        void *data) {
    struct nano_toplevel *toplevel =
        wl_container_of(listener, toplevel, request_fullscreen);

    /* Honor fullscreen requests -- on mobile this is the same as maximize */
    if (toplevel->xdg_toplevel->requested.fullscreen) {
        struct nano_output *output;
        wl_list_for_each(output, &toplevel->server->outputs, link) {
            int width, height;
            wlr_output_effective_resolution(output->wlr_output,
                &width, &height);
            wlr_xdg_toplevel_set_fullscreen(toplevel->xdg_toplevel, true);
            wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, width, height);
            wlr_scene_node_set_position(&toplevel->scene_tree->node, 0, 0);
            break;
        }
    } else {
        wlr_xdg_toplevel_set_fullscreen(toplevel->xdg_toplevel, false);
        /* Re-maximize since we're in mobile mode */
        auto_maximize_toplevel(toplevel);
    }
}

/* --- New XDG toplevel --- */

static void server_new_xdg_toplevel(struct wl_listener *listener, void *data) {
    struct nano_server *server =
        wl_container_of(listener, server, new_xdg_toplevel);
    struct wlr_xdg_toplevel *xdg_toplevel = data;

    struct nano_toplevel *toplevel = calloc(1, sizeof(*toplevel));
    if (!toplevel) {
        wlr_log(WLR_ERROR, "Failed to allocate nano_toplevel");
        return;
    }

    toplevel->server = server;
    toplevel->xdg_toplevel = xdg_toplevel;

    /* Create the scene tree node in the toplevel layer */
    toplevel->scene_tree = wlr_scene_xdg_surface_create(
        server->layer_toplevel, xdg_toplevel->base);
    if (!toplevel->scene_tree) {
        wlr_log(WLR_ERROR, "Failed to create scene tree for toplevel");
        free(toplevel);
        return;
    }

    /* Store a pointer back to the toplevel in the scene node */
    toplevel->scene_tree->node.data = toplevel;
    xdg_toplevel->base->data = toplevel->scene_tree;

    /* Register listeners */
    toplevel->map.notify = xdg_toplevel_map;
    wl_signal_add(&xdg_toplevel->base->surface->events.map, &toplevel->map);

    toplevel->unmap.notify = xdg_toplevel_unmap;
    wl_signal_add(&xdg_toplevel->base->surface->events.unmap,
        &toplevel->unmap);

    toplevel->commit.notify = xdg_toplevel_commit;
    wl_signal_add(&xdg_toplevel->base->surface->events.commit,
        &toplevel->commit);

    toplevel->destroy.notify = xdg_toplevel_destroy;
    wl_signal_add(&xdg_toplevel->events.destroy, &toplevel->destroy);

    toplevel->request_move.notify = xdg_toplevel_request_move;
    wl_signal_add(&xdg_toplevel->events.request_move,
        &toplevel->request_move);

    toplevel->request_resize.notify = xdg_toplevel_request_resize;
    wl_signal_add(&xdg_toplevel->events.request_resize,
        &toplevel->request_resize);

    toplevel->request_maximize.notify = xdg_toplevel_request_maximize;
    wl_signal_add(&xdg_toplevel->events.request_maximize,
        &toplevel->request_maximize);

    toplevel->request_fullscreen.notify = xdg_toplevel_request_fullscreen;
    wl_signal_add(&xdg_toplevel->events.request_fullscreen,
        &toplevel->request_fullscreen);
}

/* --- XDG popup handling --- */

static void xdg_popup_commit(struct wl_listener *listener, void *data) {
    struct nano_popup *popup = wl_container_of(listener, popup, commit);

    if (popup->xdg_popup->base->initial_commit) {
        /*
         * When an xdg_popup is created, we need to configure it with the
         * parent's geometry so it can position itself correctly.
         */
        wlr_xdg_popup_unconstrain_from_box(popup->xdg_popup,
            &(struct wlr_box){
                .x = -popup->xdg_popup->current.geometry.x,
                .y = -popup->xdg_popup->current.geometry.y,
                .width = INT32_MAX,
                .height = INT32_MAX,
            });
    }
}

static void xdg_popup_destroy(struct wl_listener *listener, void *data) {
    struct nano_popup *popup = wl_container_of(listener, popup, destroy);

    wl_list_remove(&popup->commit.link);
    wl_list_remove(&popup->destroy.link);

    free(popup);
}

static void server_new_xdg_popup(struct wl_listener *listener, void *data) {
    struct nano_server *server =
        wl_container_of(listener, server, new_xdg_popup);
    struct wlr_xdg_popup *xdg_popup = data;

    (void)server;

    struct nano_popup *popup = calloc(1, sizeof(*popup));
    if (!popup) {
        wlr_log(WLR_ERROR, "Failed to allocate nano_popup");
        return;
    }

    popup->xdg_popup = xdg_popup;

    /*
     * Create the popup's scene tree as a child of the parent surface's
     * scene tree. The parent could be a toplevel or another popup.
     */
    struct wlr_xdg_surface *parent =
        wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
    if (!parent) {
        wlr_log(WLR_ERROR, "Popup has no parent surface");
        free(popup);
        return;
    }

    struct wlr_scene_tree *parent_tree = parent->data;
    if (!parent_tree) {
        wlr_log(WLR_ERROR, "Parent surface has no scene tree");
        free(popup);
        return;
    }

    popup->scene_tree = wlr_scene_xdg_surface_create(
        parent_tree, xdg_popup->base);
    if (!popup->scene_tree) {
        wlr_log(WLR_ERROR, "Failed to create scene tree for popup");
        free(popup);
        return;
    }

    xdg_popup->base->data = popup->scene_tree;

    popup->commit.notify = xdg_popup_commit;
    wl_signal_add(&xdg_popup->base->surface->events.commit, &popup->commit);

    popup->destroy.notify = xdg_popup_destroy;
    wl_signal_add(&xdg_popup->events.destroy, &popup->destroy);
}

void nano_xdg_init(struct nano_server *server) {
    server->xdg_shell = wlr_xdg_shell_create(server->display, 3);
    if (!server->xdg_shell) {
        wlr_log(WLR_ERROR, "Failed to create XDG shell");
        return;
    }

    server->new_xdg_toplevel.notify = server_new_xdg_toplevel;
    wl_signal_add(&server->xdg_shell->events.new_toplevel,
        &server->new_xdg_toplevel);

    server->new_xdg_popup.notify = server_new_xdg_popup;
    wl_signal_add(&server->xdg_shell->events.new_popup,
        &server->new_xdg_popup);
}
