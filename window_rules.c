#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_keyboard.h>
#include "server.h"
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <stdlib.h>

/* =========================================================================
 * INTERNAL HELPERS
 * ========================================================================= */

static inline bool view_is_linked(struct tiny_view *view) {
    return view->link.prev != NULL && view->link.next != NULL;
}

/* =========================================================================
 * FOCUS
 * ========================================================================= */

void focus_view(struct tiny_view *view, struct wlr_surface *surface) {
    if (!view || !view->server || !view->server->seat || !surface) return;

    struct tiny_server *server = view->server;
    struct wlr_seat    *seat   = server->seat;

    if (!view->xdg_toplevel) return;

    struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;
    if (prev_surface == surface) return;

    /* De-activate the previously focused toplevel */
    if (prev_surface) {
        struct wlr_xdg_surface *prev_xdg =
            wlr_xdg_surface_try_from_wlr_surface(prev_surface);
        if (prev_xdg && prev_xdg->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL)
            wlr_xdg_toplevel_set_activated(prev_xdg->toplevel, false);
    }

    /* Raise visually in the scene graph ONLY.
     * CRITICAL: do NOT reorder server->views.  That list is the tiling
     * layout's source of truth — moving entries would make retile_all_views()
     * silently reassign the wrong positions to every window. */
    wlr_scene_node_raise_to_top(&view->scene_tree->node);
    server->focused_view = view;

    /* Activate and hand over keyboard focus */
    wlr_xdg_toplevel_set_activated(view->xdg_toplevel, true);
    wlr_xdg_surface_schedule_configure(view->xdg_surface);
    wlr_seat_keyboard_notify_enter(seat, surface, NULL, 0, NULL);

    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
    if (keyboard)
        wlr_seat_keyboard_send_modifiers(seat, &keyboard->modifiers);

    /* Update border visibility: hide all, show only the focused one */
    struct tiny_view *v;
    wl_list_for_each(v, &server->views, link) {
        if (v->border)
            wlr_scene_node_set_enabled(&v->border->node, v == view);
    }
}

/* =========================================================================
 * DECORATION
 * ========================================================================= */

void server_new_decoration(struct wl_listener *listener, void *data) {
    struct wlr_xdg_toplevel_decoration_v1 *decoration = data;
    wlr_xdg_toplevel_decoration_v1_set_mode(
        decoration, WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

/* =========================================================================
 * SURFACE → VIEW LOOKUP
 * ========================================================================= */

struct tiny_view *find_view_from_surface(struct tiny_server *server,
                                         struct wlr_surface  *surface) {
    struct tiny_view *view;
    wl_list_for_each(view, &server->views, link) {
        if (view->xdg_surface->surface == surface)
            return view;
    }
    return NULL;
}

/* =========================================================================
 * MOUSE-HOVER TRACKING
 * ========================================================================= */

void event_mouse_hover_over_client(struct tiny_server *server) {
    double sx, sy;
    struct wlr_scene_node *node = wlr_scene_node_at(
        &server->scene->tree.node,
        server->cursor->x, server->cursor->y, &sx, &sy);

    struct tiny_view *hovered_view = NULL;

    if (node) {
        struct wlr_scene_tree *tree = node->parent;
        while (tree) {
            if (tree->node.data) {
                hovered_view = tree->node.data;
                break;
            }
            tree = tree->node.parent;
        }
    }

    server->mouse_over_view           = hovered_view;
    server->mouse_over_client_surface =
        hovered_view ? hovered_view->xdg_surface->surface : NULL;
}

/* =========================================================================
 * APPLY TILING GEOMETRY TO ONE VIEW
 * ========================================================================= */

void apply_tiling_to_view(struct tiny_view *view) {
    if (!view || !view->xdg_toplevel) return;

    wlr_scene_node_set_position(&view->scene_tree->node, view->x, view->y);
    wlr_xdg_toplevel_set_size(view->xdg_toplevel, view->width, view->height);
    wlr_xdg_surface_schedule_configure(view->xdg_surface);

    /* Resize the border rect to match the new window dimensions.
     *
     * The border must be created as a CHILD of view->scene_tree so its
     * coordinates are relative to the window, not the screen.  This means:
     *   - Do NOT call wlr_scene_node_set_position on it here.
     *   - Its position stays at (0, 0) relative to the scene_tree always.
     *   - It moves with the window automatically because it is a child node.
     *
     * Correct one-time setup in your map/create handler:
     *   float col[4] = {1.f, 0.f, 0.f, 1.f};                        // red
     *   view->border = wlr_scene_rect_create(view->scene_tree,
     *                                        view->width, view->height, col);
     *   wlr_scene_node_lower_to_bottom(&view->border->node);  // behind content
     *   wlr_scene_node_set_enabled(&view->border->node, false); // hidden until focused
     */
    if (view->border)
        wlr_scene_rect_set_size(view->border, view->width, view->height);
}

/* =========================================================================
 * HYPRLAND-STYLE TILING  (initial placement)
 * ========================================================================= */

void hyprland_style_tiling(struct tiny_view *new_view) {
    struct tiny_server *server = new_view->server;
    const int32_t GAP      = 10;
    const int32_t MIN_SIZE = 60;

    /* ---- Case A: very first window — fill the screen ---- */
    if (wl_list_empty(&server->views)) {
        new_view->x      = server->usable_screen_area.x      + GAP;
        new_view->y      = server->usable_screen_area.y      + GAP;
        new_view->width  = server->usable_screen_area.width  - GAP * 2;
        new_view->height = server->usable_screen_area.height - GAP * 2;

        wl_list_insert(&server->views, &new_view->link);
        apply_tiling_to_view(new_view);
        return;
    }

    /* ---- Case B: split the hovered (or most-recently-focused) window ---- */
    struct tiny_view *target = server->mouse_over_view;
    if (!target)
        target = wl_container_of(server->views.next, target, link);
    if (!target || target == new_view) return;

    int32_t tw = target->width,  th = target->height;
    int32_t tx = target->x,      ty = target->y;

    
    bool split_vertically = (tw >= th);

    if (split_vertically) {
        /* Side-by-side */
        int32_t half_w = (tw - GAP) / 2;
        if (half_w < MIN_SIZE) return;

        target->width = half_w;

        new_view->x      = tx + half_w + GAP;
        new_view->y      = ty;
        new_view->width  = tw - half_w - GAP;  /* use remainder, not half_w, to
                                                   avoid 1 px gap from integer div */
        new_view->height = th;
    } else {
        /* Stacked */
        int32_t half_h = (th - GAP) / 2;
        if (half_h < MIN_SIZE) return;

        target->height = half_h;

        new_view->x      = tx;
        new_view->y      = ty + half_h + GAP;
        new_view->width  = tw;
        new_view->height = th - half_h - GAP;
    }

    new_view->sibling = target;
    target->sibling   = new_view;

    wl_list_insert(&server->views, &new_view->link);

    apply_tiling_to_view(target);
    apply_tiling_to_view(new_view);
}

/* =========================================================================
 * RETILE ALL VIEWS  — master-stack layout
 *
 * Called after a window is closed (or on any full layout invalidation).
 * Never moves views in/out of the list during calculation — reads the list
 * with wl_list_for_each and only calls apply_tiling_to_view at the end.
 *
 *   ┌──────────┬───────────┐
 *   │          │  stack 0  │
 *   │  master  ├───────────┤
 *   │          │  stack 1  │
 *   └──────────┴───────────┘
 * ========================================================================= */

void retile_all_views(struct tiny_server *server) {
    if (wl_list_empty(&server->views)) return;

    const int32_t GAP = 10;

    int32_t usable_x = server->usable_screen_area.x      + GAP;
    int32_t usable_y = server->usable_screen_area.y      + GAP;
    int32_t usable_w = server->usable_screen_area.width  - GAP * 2;
    int32_t usable_h = server->usable_screen_area.height - GAP * 2;

    int count = wl_list_length(&server->views);
    int32_t master_w = (count == 1) ? usable_w : (usable_w - GAP) / 2;

    struct tiny_view *v;
    int i = 0;
    wl_list_for_each(v, &server->views, link) {
        if (count == 1) {
            v->x = usable_x; v->y = usable_y;
            v->width = usable_w; v->height = usable_h;
        } else if (i == 0) {
            /* Master: left half */
            v->x = usable_x; v->y = usable_y;
            v->width  = master_w;
            v->height = usable_h;
        } else {
            /* Stack: right half divided evenly */
            int     stack_count = count - 1;
            int32_t stack_x     = usable_x + master_w + GAP;
            int32_t stack_w     = usable_w - master_w - GAP;
            int32_t slot_h      = (usable_h - GAP * (stack_count - 1)) / stack_count;

            v->x      = stack_x;
            v->y      = usable_y + (i - 1) * (slot_h + GAP);
            v->width  = stack_w;
            v->height = slot_h;
        }
        apply_tiling_to_view(v);
        i++;
    }
}

/* =========================================================================
 * NEIGHBOUR DETECTION  (shared for move and resize)
 *
 * direction: 0=left 1=right 2=up 3=down
 *
 * We check exact GAP-distance edge adjacency rather than a fuzzy pixel
 * range, because the tiler always places windows exactly GAP px apart.
 * A 2 px tolerance handles integer-division rounding in the layout.
 * ========================================================================= */

static struct tiny_view *find_neighbour(struct tiny_server *server,
                                        struct tiny_view   *src,
                                        int                 direction) {
    const int32_t GAP       = 10;
    const int32_t TOLERANCE = 2;

    struct tiny_view *best      = NULL;
    int32_t           best_dist = INT32_MAX;

    struct tiny_view *v;
    wl_list_for_each(v, &server->views, link) {
        if (v == src) continue;

        bool    edge_match = false;
        int32_t dist       = 0;

        switch (direction) {
        case 0: /* left */
            edge_match = abs((v->x + v->width + GAP) - src->x) <= TOLERANCE
                      && v->y          < src->y + src->height
                      && v->y + v->height > src->y;
            dist = src->x - (v->x + v->width);
            break;
        case 1: /* right */
            edge_match = abs(v->x - (src->x + src->width + GAP)) <= TOLERANCE
                      && v->y          < src->y + src->height
                      && v->y + v->height > src->y;
            dist = v->x - (src->x + src->width);
            break;
        case 2: /* up */
            edge_match = abs((v->y + v->height + GAP) - src->y) <= TOLERANCE
                      && v->x          < src->x + src->width
                      && v->x + v->width > src->x;
            dist = src->y - (v->y + v->height);
            break;
        case 3: /* down */
            edge_match = abs(v->y - (src->y + src->height + GAP)) <= TOLERANCE
                      && v->x          < src->x + src->width
                      && v->x + v->width > src->x;
            dist = v->y - (src->y + src->height);
            break;
        }

        if (edge_match && dist < best_dist) {
            best_dist = dist;
            best      = v;
        }
    }
    return best;
}

/* =========================================================================
 * WINDOW SWAP
 * ========================================================================= */

void swap_views(struct tiny_view *a, struct tiny_view *b) {
    if (!a || !b || a == b) return;

    /* Swap list positions, not geometry.  The layout function owns
     * coordinates — we change the ordering and let retile_all_views()
     * recompute.  Swapping x/y/w/h directly would be undone by the
     * next retile call. */

    struct wl_list tmp;
    wl_list_insert(&a->link, &tmp); /* placeholder at a's old position */
    wl_list_remove(&a->link);
    wl_list_insert(&b->link, &a->link);   /* a goes to b's position */
    wl_list_remove(&b->link);
    wl_list_insert(&tmp, &b->link);       /* b goes to the placeholder */
    wl_list_remove(&tmp);

    retile_all_views(a->server);
}

/* Public entry point — call from your keybind handler */
void move_focused_window(struct tiny_server *server, int direction) {
    if (!server->focused_view) return;

    struct tiny_view *neighbour =
        find_neighbour(server, server->focused_view, direction);
        printf("Trying To find neighnour");
    if (neighbour)
        swap_views(server->focused_view, neighbour);
}

/* =========================================================================
 * WINDOW RESIZE
 *
 * axis:  0 = horizontal (width),  1 = vertical (height)
 * delta: pixels — positive = grow, negative = shrink
 *
 * Snapshots the old edges before changing the focused window, then finds
 * neighbours whose edges were exactly GAP away and adjusts them.
 * ========================================================================= */

void resize_focused_window(struct tiny_server *server,
                            int axis, int32_t delta) {
    if (!server->focused_view) return;

    const int32_t MIN_SIZE  = 60;
    const int32_t GAP       = 10;
    const int32_t TOLERANCE = 2;

    struct tiny_view *focus = server->focused_view;

    if (axis == 0) {
        /* ---- Horizontal ---- */
        int32_t old_right = focus->x + focus->width;
        int32_t old_left  = focus->x;

        int32_t new_w = focus->width + delta;
        if (new_w < MIN_SIZE) new_w = MIN_SIZE;
        int32_t actual = new_w - focus->width;
        if (actual == 0) return;

        focus->width = new_w;
        apply_tiling_to_view(focus);

        struct tiny_view *v;
        wl_list_for_each(v, &server->views, link) {
            if (v == focus) continue;

            bool shares_y = v->y < focus->y + focus->height
                         && v->y + v->height > focus->y;

            if (shares_y && abs(v->x - (old_right + GAP)) <= TOLERANCE) {
                /* Right neighbour: shift and shrink */
                v->x     += actual;
                v->width -= actual;
                if (v->width < MIN_SIZE) v->width = MIN_SIZE;
                apply_tiling_to_view(v);
            } else if (shares_y
                    && abs((v->x + v->width) - (old_left - GAP)) <= TOLERANCE
                    && actual < 0) {
                /* Left neighbour: expand into freed space */
                v->width -= actual; /* actual is negative, so width grows */
                if (v->width < MIN_SIZE) v->width = MIN_SIZE;
                apply_tiling_to_view(v);
            }
        }
    } else {
        /* ---- Vertical ---- */
        int32_t old_bottom = focus->y + focus->height;
        int32_t old_top    = focus->y;

        int32_t new_h = focus->height + delta;
        if (new_h < MIN_SIZE) new_h = MIN_SIZE;
        int32_t actual = new_h - focus->height;
        if (actual == 0) return;

        focus->height = new_h;
        apply_tiling_to_view(focus);

        struct tiny_view *v;
        wl_list_for_each(v, &server->views, link) {
            if (v == focus) continue;

            bool shares_x = v->x < focus->x + focus->width
                         && v->x + v->width > focus->x;

            if (shares_x && abs(v->y - (old_bottom + GAP)) <= TOLERANCE) {
                /* Bottom neighbour: shift and shrink */
                v->y      += actual;
                v->height -= actual;
                if (v->height < MIN_SIZE) v->height = MIN_SIZE;
                apply_tiling_to_view(v);
            } else if (shares_x
                    && abs((v->y + v->height) - (old_top - GAP)) <= TOLERANCE
                    && actual < 0) {
                /* Top neighbour: expand into freed space */
                v->height -= actual;
                if (v->height < MIN_SIZE) v->height = MIN_SIZE;
                apply_tiling_to_view(v);
            }
        }
    }
}

/*void close_focused_view(struct tiny_server *server) {
    // Use the actual focus pointer instead of the list head
    struct tiny_view *view = server->focused_view;

    if (view && view->xdg_toplevel) {
        printf("Sending close request to: %s\n", 
               view->xdg_toplevel->title ? view->xdg_toplevel->title : "Unknown");
        
        wlr_xdg_toplevel_send_close(view->xdg_toplevel);
    } else {
        printf("No focused view found to close.\n");
    }
}*/

void close_focused_view(struct tiny_server *server) {
    if (server->focused_view && server->focused_view->xdg_toplevel) {
        wlr_xdg_toplevel_send_close(server->focused_view->xdg_toplevel);
    }
}
