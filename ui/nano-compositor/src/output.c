#define WLR_USE_UNSTABLE
#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>

#include "nano-compositor.h"

static void output_frame(struct wl_listener *listener, void *data) {
    struct nano_output *output = wl_container_of(listener, output, frame);
    struct wlr_scene *scene = output->server->scene;

    struct wlr_scene_output *scene_output =
        wlr_scene_get_scene_output(scene, output->wlr_output);
    if (!scene_output) {
        return;
    }

    /* Render the scene and commit the output */
    wlr_scene_output_commit(scene_output, NULL);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(scene_output, &now);
}

static void output_request_state(struct wl_listener *listener, void *data) {
    struct nano_output *output =
        wl_container_of(listener, output, request_state);
    const struct wlr_output_event_request_state *event = data;

    wlr_output_commit_state(output->wlr_output, event->state);
}

static void output_destroy(struct wl_listener *listener, void *data) {
    struct nano_output *output = wl_container_of(listener, output, destroy);

    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->request_state.link);
    wl_list_remove(&output->destroy.link);
    wl_list_remove(&output->link);

    free(output);
}

static void server_new_output(struct wl_listener *listener, void *data) {
    struct nano_server *server =
        wl_container_of(listener, server, new_output);
    struct wlr_output *wlr_output = data;

    /* Initialize the output with the allocator and renderer */
    wlr_output_init_render(wlr_output, server->allocator, server->renderer);

    /* Set the preferred mode if available */
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);

    struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
    if (mode) {
        wlr_output_state_set_mode(&state, mode);
    }

    /* Commit the output state */
    wlr_output_commit_state(wlr_output, &state);
    wlr_output_state_finish(&state);

    /* Create our output wrapper */
    struct nano_output *output = calloc(1, sizeof(*output));
    if (!output) {
        wlr_log(WLR_ERROR, "Failed to allocate nano_output");
        return;
    }

    output->server = server;
    output->wlr_output = wlr_output;

    /* Register listeners */
    output->frame.notify = output_frame;
    wl_signal_add(&wlr_output->events.frame, &output->frame);

    output->request_state.notify = output_request_state;
    wl_signal_add(&wlr_output->events.request_state, &output->request_state);

    output->destroy.notify = output_destroy;
    wl_signal_add(&wlr_output->events.destroy, &output->destroy);

    wl_list_insert(&server->outputs, &output->link);

    /*
     * Add the output to the layout. For a mobile device we assume a single
     * output and place it at the origin automatically.
     */
    struct wlr_output_layout_output *l_output =
        wlr_output_layout_add_auto(server->output_layout, wlr_output);
    if (!l_output) {
        wlr_log(WLR_ERROR, "Failed to add output to layout");
        return;
    }

    output->scene_output =
        wlr_scene_output_create(server->scene, wlr_output);
    if (!output->scene_output) {
        wlr_log(WLR_ERROR, "Failed to create scene output");
        return;
    }

    wlr_scene_output_layout_add_output(
        server->scene_layout, l_output, output->scene_output);

    /* Arrange layer surfaces for the new output */
    nano_arrange_layers(output);
}

void nano_output_init(struct nano_server *server) {
    server->new_output.notify = server_new_output;
    wl_signal_add(&server->backend->events.new_output, &server->new_output);
}
