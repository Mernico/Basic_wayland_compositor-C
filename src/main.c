#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/wayland.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

struct way_comp {
    struct wl_display *wl_display;
    struct wlr_backend *backend;
    struct wlr_renderer *renderer;
    struct wlr_allocator *allocator;
    struct wlr_scene *scene;

    struct wl_listener new_output;
};

static void new_output_notify(struct wl_listener *listener, void *data) {
    struct way_comp *server = wl_container_of(listener, server, new_output);
    struct wlr_output *wlr_output = data;

    // Configures the output to use the renderer
    wlr_output_init_render(wlr_output, server->allocator, server->renderer);

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);

    // Set the mode (resolution/refresh rate)
    struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
    if (mode != NULL) {
        wlr_output_state_set_mode(&state, mode);
    }

    wlr_output_commit_state(wlr_output, &state);
    wlr_output_state_finish(&state);

    // Create a scene output (bridges the scene graph and the physical output)
    struct wlr_scene_output *scene_output = wlr_scene_output_create(server->scene, wlr_output);
    
    // Create a Red Rectangle as a background
    // This lives in the scene graph; wlroots will redraw it automatically
    wlr_scene_rect_create(&server->scene->tree, 
        wlr_output->width, wlr_output->height, (float[]){1.0, 0.0, 0.0, 1.0});
    wlr_scene_output_commit(scene_output, NULL);
}

int main(int argc, char **argv) {
    wlr_log_init(WLR_DEBUG, NULL);

    struct way_comp server = {0};
    server.wl_display = wl_display_create();
    struct wl_event_loop *event_loop = wl_display_get_event_loop(server.wl_display);

    // Backend setup
    const char *remote_display_name = getenv("WAYLAND_DISPLAY");
    if (remote_display_name) {
        struct wl_display *remote_display = wl_display_connect(remote_display_name);
        server.backend = wlr_wl_backend_create(event_loop, remote_display);
    } else {
        server.backend = wlr_backend_autocreate(event_loop, NULL);
    }

    server.renderer = wlr_renderer_autocreate(server.backend);
    wlr_renderer_init_wl_display(server.renderer, server.wl_display);

    server.allocator = wlr_allocator_autocreate(server.backend, server.renderer);
    
    // Initialize the Scene Graph
    server.scene = wlr_scene_create();

    server.new_output.notify = new_output_notify;
    wl_signal_add(&server.backend->events.new_output, &server.new_output);

    const char *socket = wl_display_add_socket_auto(server.wl_display);
    if (!socket) return 1;

if (!wlr_backend_start(server.backend)) return 1;

    // MANDATORY KICK:
    // This processes the 'Handshake' with GNOME so new_output_notify actually runs.
    wl_event_loop_dispatch(event_loop, 0); 

    printf("Compositor running on WAYLAND_DISPLAY=%s\n", socket);
    setenv("WAYLAND_DISPLAY", socket, true);

    // This keeps the compositor alive
    wl_display_run(server.wl_display);

    wl_display_destroy(server.wl_display);
    return 0;
}
