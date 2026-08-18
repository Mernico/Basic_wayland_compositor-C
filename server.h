#ifndef TINY_SERVER_H
#define TINY_SERVER_H
#define _POSIX_C_SOURCE 200809L
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <xkbcommon/xkbcommon.h>

struct tiny_server {
    struct wl_display          *wl_display;
    struct wlr_backend         *backend;
    struct wlr_renderer        *renderer;
    struct wlr_allocator       *allocator;
    struct wlr_scene           *scene;
    struct wlr_xdg_shell       *xdg_shell;
    struct wl_listener          new_xdg_surface;
    struct wl_list              views;          /* tiling order — do not reorder for focus */
    struct tiny_view           *focused_view;   /* currently focused view (not list position) */
    struct wlr_cursor          *cursor;
    struct wlr_xcursor_manager *cursor_mgr;
    struct wl_listener          cursor_motion;
    struct wl_listener          cursor_motion_absolute;
    struct wl_listener          cursor_button;
    struct wlr_seat            *seat;
    struct wl_listener          new_input;
    struct wl_list              keyboards;
    struct wlr_output_layout   *output_layout;
    struct wl_listener          new_output;
    struct wl_list              outputs;
    struct wlr_box              usable_screen_area;
    int32_t                     screen_width;
    int32_t                     screen_height;
    struct wlr_layer_shell_v1  *layer_shell;
    struct wl_listener          new_layer_shell_surface;
    struct wlr_surface         *mouse_over_client_surface;
    struct tiny_view           *mouse_over_view;
    struct wlr_surface         *old_mouse_over_client_surface;
};

struct tiny_view {
    struct wl_list              link;
    struct tiny_server         *server;
    struct wlr_xdg_surface     *xdg_surface;
    struct wlr_xdg_toplevel    *xdg_toplevel;
    struct wlr_scene_tree      *scene_tree;
    struct wl_listener          map;
    struct wl_listener          unmap;
    struct wl_listener          destroy;
    struct wl_listener          request_configure;
    struct wl_listener          commit;
    struct wl_listener          configure;

    /* Tiling geometry — owned by the layout functions */
    int32_t                     x, y, width, height;
    int                         position_index;
    struct tiny_view           *sibling;

    /* Border highlight rect.
     *
     * MUST be created as a child of scene_tree, not the root scene:
     *   float col[4] = {1.f, 0.f, 0.f, 1.f};
     *   view->border = wlr_scene_rect_create(view->scene_tree, w, h, col);
     *   wlr_scene_node_set_position(&view->border->node, 0, 0);
     *   wlr_scene_node_lower_to_bottom(&view->border->node);
     *   wlr_scene_node_set_enabled(&view->border->node, false);
     *
     * Because it is a child node, it moves with the window automatically.
     * Never call wlr_scene_node_set_position on it after creation.       */
    struct wlr_scene_rect      *border;
};

struct tiny_keyboard {
    struct wl_list              link;
    struct tiny_server         *server;
    struct wlr_keyboard        *wlr_keyboard;
    struct wl_listener          key;
    struct wl_listener          modifiers; /* REQUIRED: forwards Shift/CapsLock/etc to seat */
};

/* -------------------------------------------------------------------------
 * Function declarations
 * --------------------------------------------------------------------- */

/* Input / output */
void server_new_input(struct wl_listener *listener, void *data);
void server_new_output(struct wl_listener *listener, void *data);
void server_new_xdg_surface(struct wl_listener *listener, void *data);
void server_new_layer_shell_surface(struct wl_listener *listener, void *data);

/* Cursor */
void server_cursor_motion(struct wl_listener *listener, void *data);
void server_cursor_motion_absolute(struct wl_listener *listener, void *data);
void event_mouse_hover_over_client(struct tiny_server *server);

/* Focus */
void focus_view(struct tiny_view *view, struct wlr_surface *surface);
void window_focus_on_click(struct wl_listener *listener, void *data);

/* Keyboard */
void keyboard_handle_key(struct wl_listener *listener, void *data);
void close_focused_view(struct tiny_server *server);

/* Tiling — placement */
void hyprland_style_tiling(struct tiny_view *new_view);
void retile_all_views(struct tiny_server *server);
void apply_tiling_to_view(struct tiny_view *view);

/* Tiling — interaction
 *   direction: 0=left 1=right 2=up 3=down
 *   axis:      0=horizontal(width)  1=vertical(height)
 *   delta:     pixels, positive=grow negative=shrink                    */
void swap_views(struct tiny_view *a, struct tiny_view *b);
void move_focused_window(struct tiny_server *server, int direction);
void resize_focused_window(struct tiny_server *server, int axis, int32_t delta);
void autostart(struct tiny_server *server);

/* Utility */
struct tiny_view *find_view_from_surface(struct tiny_server *server,
                                          struct wlr_surface  *surface);

#endif /* TINY_SERVER_H */

