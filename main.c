#define _POSIX_C_SOURCE 200112L
#include <wlr/util/log.h>
#include <wlr/types/wlr_compositor.h>
#include "server.h"
#include <stdlib.h>
#include <unistd.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <linux/input-event-codes.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <xkbcommon/xkbcommon.h>
#include "server.h"
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>

int main(int argc, char *argv[]) {
    wlr_log_init(WLR_DEBUG, NULL);
    struct tiny_server server = {0};

    server.wl_display = wl_display_create();
    server.backend = wlr_backend_autocreate(wl_display_get_event_loop(server.wl_display), NULL);
    server.renderer = wlr_renderer_autocreate(server.backend);
    wlr_renderer_init_wl_display(server.renderer, server.wl_display);

    server.allocator = wlr_allocator_autocreate(server.backend, server.renderer);


    wlr_compositor_create(server.wl_display, 6, server.renderer);
    wlr_subcompositor_create(server.wl_display);
    wlr_data_device_manager_create(server.wl_display);
    wlr_renderer_init_wl_display(server.renderer, server.wl_display); 



    server.scene = wlr_scene_create();
    
    //Backround is Orange
    float orange[4] = {1.0f, 0.5f, 0.0f, 1.0f};
    float gray[4] = {0.5f, 0.5f, 0.5f, 1.0f};
    float dark_bg[4] = {0.12f, 0.12f, 0.14f, 1.0f};
    struct wlr_scene_rect *background_rect = wlr_scene_rect_create(
        &server.scene->tree, 
        2560, 1440, // Initial size (Width, Height)
        dark_bg      // The color array
    );
    // --- Background / UI Elements ---
    struct wlr_scene_rect *blue_square = wlr_scene_rect_create(
        &server.scene->tree, 200, 200, (float[]){0, 0, 1, 1});
    wlr_scene_node_set_position(&blue_square->node, 500, 500);

    // --- Output Layout & Manager ---
    server.output_layout = wlr_output_layout_create(server.wl_display);
    
    
    wlr_scene_attach_output_layout(server.scene, server.output_layout);
    
    // THIS IS THE KEY FOR WAYBAR:
    wlr_xdg_output_manager_v1_create(server.wl_display, server.output_layout);

    // --- Input / Cursor ---
    server.cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(server.cursor, server.output_layout);
    server.cursor_mgr = wlr_xcursor_manager_create(NULL, 24);

    server.cursor_motion.notify = server_cursor_motion;
    wl_signal_add(&server.cursor->events.motion, &server.cursor_motion);
    server.cursor_motion_absolute.notify = server_cursor_motion_absolute;
    wl_signal_add(&server.cursor->events.motion_absolute, &server.cursor_motion_absolute);

    server.seat = wlr_seat_create(server.wl_display, "seat0");
    wlr_seat_set_capabilities(server.seat, WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD);
    
    wl_list_init(&server.keyboards);
    server.new_input.notify = server_new_input;
    wl_signal_add(&server.backend->events.new_input, &server.new_input);

    // --- Shells (Windows & Bars) ---
    wl_list_init(&server.views);
    server.xdg_shell = wlr_xdg_shell_create(server.wl_display, 3);
    server.new_xdg_surface.notify = server_new_xdg_surface;
    wl_signal_add(&server.xdg_shell->events.new_surface, &server.new_xdg_surface);

    // Layer Shell for Waybar
    server.layer_shell = wlr_layer_shell_v1_create(server.wl_display, 4);
    server.new_layer_shell_surface.notify = server_new_layer_shell_surface;
    wl_signal_add(&server.layer_shell->events.new_surface, &server.new_layer_shell_surface);


    wlr_xdg_activation_v1_create(server.wl_display);

    wl_list_init(&server.outputs);
    server.new_output.notify = server_new_output;
    wl_signal_add(&server.backend->events.new_output, &server.new_output);

    const char *socket = wl_display_add_socket_auto(server.wl_display);
    

 
    server.cursor_button.notify = window_focus_on_click;
    wl_signal_add(&server.cursor->events.button, &server.cursor_button);


    wlr_xcursor_manager_load(server.cursor_mgr, 1.0);
    wlr_cursor_set_xcursor(server.cursor, server.cursor_mgr, "left_ptr");

    wlr_log(WLR_INFO, "Running on WAYLAND_DISPLAY=%s", socket);


    wl_display_flush_clients(server.wl_display);
    wl_event_loop_dispatch(wl_display_get_event_loop(server.wl_display), 0);


    wlr_backend_start(server.backend);

    setenv("WAYLAND_DISPLAY", socket, true); 
    autostart(&server);                     

    wl_display_run(server.wl_display);
      

    
    wl_display_destroy_clients(server.wl_display);
    wl_display_destroy(server.wl_display);  
    return 0;
}
