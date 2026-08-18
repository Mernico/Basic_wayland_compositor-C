#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <time.h>
#include <wlr/types/wlr_output.h>
#include "server.h"

struct tiny_output {
    struct wl_list link;
    struct tiny_server *server;
    struct wlr_output *wlr_output;
    struct wl_listener frame;
    struct wl_listener destroy;
};

static void output_frame(struct wl_listener *listener, void *data) {
    struct tiny_output *output = wl_container_of(listener, output, frame);
    struct wlr_scene_output *scene_output = wlr_scene_get_scene_output(
        output->server->scene, output->wlr_output);

    wlr_scene_output_commit(scene_output, NULL);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(scene_output, &now);
}

static void output_destroy(struct wl_listener *listener, void *data) {
    struct tiny_output *output = wl_container_of(listener, output, destroy);
    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->destroy.link);
    wl_list_remove(&output->link);
    free(output);
}

void server_new_output(struct wl_listener *listener, void *data) {
    struct tiny_server *server = wl_container_of(listener, server, new_output);
    struct wlr_output *wlr_output = data;
    printf("SERVER NEW OUTPUT FUNCTION TRIGGERED\n");
    // 1. Initialize render
    wlr_output_init_render(wlr_output, server->allocator, server->renderer);

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);

    // 2. Try preferred mode, but FALLBACK to current dimensions if NULL
    struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
    if (mode != NULL) {
        wlr_output_state_set_mode(&state, mode);
        server->screen_width = mode->width;
        server->screen_height = mode->height;
    } else {
        // This is the fix for the 0x0 issue
        server->screen_width = wlr_output->width;
        server->screen_height = wlr_output->height;
    }

    // If it's STILL 0 (rare), set a hard default so we don't crash
    if (server->screen_width == 0) server->screen_width = 1280;
    if (server->screen_height == 0) server->screen_height = 720;

    server->usable_screen_area.x = 0;
    server->usable_screen_area.y = 0;
    server->usable_screen_area.width = server->screen_width;
    server->usable_screen_area.height = server->screen_height;

    printf("Fixed Monitor Detection: %dx%d\n", server->screen_width, server->screen_height);

    wlr_output_commit_state(wlr_output, &state);
    wlr_output_state_finish(&state);

    struct tiny_output *output = calloc(1, sizeof(struct tiny_output));
    output->wlr_output = wlr_output;
    output->server = server;
    output->frame.notify = output_frame;
    wl_signal_add(&wlr_output->events.frame, &output->frame);
    output->destroy.notify = output_destroy;
    wl_signal_add(&wlr_output->events.destroy, &output->destroy);
    wl_list_insert(&server->outputs, &output->link);

    wlr_output_layout_add_auto(server->output_layout, wlr_output);
    wlr_scene_output_create(server->scene, wlr_output);
}
