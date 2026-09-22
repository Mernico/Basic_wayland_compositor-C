#include <stdlib.h>
#include <stdio.h>
#include "server.h"
#include <wlr/types/wlr_layer_shell_v1.h>

/* =========================================================================
 * XDG TOPLEVEL LIFECYCLE
 * ========================================================================= */

static void xdg_toplevel_map(struct wl_listener *listener, void *data) {
    struct tiny_view *view = wl_container_of(listener, view, map);

    /* hyprland_style_tiling inserts the view into server->views and calls
     * apply_tiling_to_view, so the scene node is already positioned before
     * we raise it. */
    hyprland_style_tiling(view);

    /* Raise after tiling so the new window is on top visually */
    wlr_scene_node_raise_to_top(&view->scene_tree->node);

    focus_view(view, view->xdg_surface->surface);

    printf("SUCCESS: Window Mapped! Title: %s\n",
           view->xdg_toplevel->title ? view->xdg_toplevel->title : "(null)");
}

static void xdg_toplevel_unmap(struct wl_listener *listener, void *data) {
    struct tiny_view *view = wl_container_of(listener, view, unmap);

    
    if (view->link.next == &view->link) return;

    /* Clear focused_view before removing so no dangling pointer is left */
    if (view->server->focused_view == view)
        view->server->focused_view = NULL;

    /* Clear mouse-over pointer for the same reason */
    if (view->server->mouse_over_view == view)
        view->server->mouse_over_view = NULL;

    struct tiny_view *sib = view->sibling;

    wl_list_remove(&view->link);
    wl_list_init(&view->link);   /* mark as detached so destroy() is safe */

    /* Expand the sibling to fill the gap left by the closed window.
     * We check that the sibling is still alive (still has a valid toplevel)
     * AND that it is still in the views list (its link is not self-pointing). */
    if (sib && sib->xdg_toplevel && sib->link.next != &sib->link) {
        bool split_was_vertical =
            (view->y == sib->y && view->height == sib->height);

        if (split_was_vertical) {
            /* They were side-by-side: give horizontal space back */
            sib->width += view->width + 10; /* +GAP */
            /* If the dead view was to the LEFT, move sibling left too */
            if (view->x < sib->x)
                sib->x = view->x;
        } else {
            /* They were stacked: give vertical space back */
            sib->height += view->height + 10; /* +GAP */
            /* If the dead view was ABOVE, move sibling up */
            if (view->y < sib->y)
                sib->y = view->y;
        }

        apply_tiling_to_view(sib);
        sib->sibling = NULL;
    } else {
        /* No valid sibling (e.g. last window, or sibling already closed).
         * retile_all_views gives every remaining window a clean layout. */
        retile_all_views(view->server);
    }

    /* After removing a window, focus the first remaining one if any */
    if (!wl_list_empty(&view->server->views)) {
        struct tiny_view *next =
            wl_container_of(view->server->views.next, next, link);
        focus_view(next, next->xdg_surface->surface);
    }
}

static void xdg_toplevel_destroy(struct wl_listener *listener, void *data) {
    struct tiny_view *view = wl_container_of(listener, view, destroy);

    /* Remove from views list only if still linked.
     * Normally unmap() already removed it — this is a safety net for
     * compositors that destroy without unmapping (rare but possible). */
    if (view->link.next != &view->link) {
        wl_list_remove(&view->link);
        wl_list_init(&view->link);
    }

    /* Clear server pointers that might still point here */
    if (view->server->focused_view == view)
        view->server->focused_view = NULL;
    if (view->server->mouse_over_view == view)
        view->server->mouse_over_view = NULL;

   
    wl_list_remove(&view->map.link);
    wl_list_remove(&view->unmap.link);
    wl_list_remove(&view->destroy.link);

    /* commit listener is removed in xdg_surface_handle_commit after the
     * initial commit fires, so check before removing again. */
    if (view->commit.link.next != &view->commit.link)
        wl_list_remove(&view->commit.link);

    free(view);
}

/* Called on every surface commit.  We only care about the very first one,
 * which signals that the xdg_surface role has been fully assigned and we
 * can safely read xdg_surface->toplevel. */
static void xdg_surface_handle_commit(struct wl_listener *listener, void *data) {
    struct tiny_view *view = wl_container_of(listener, view, commit);

    if (!view->xdg_surface->initial_commit) return;

    /* Now the role is guaranteed to be set */
    view->xdg_toplevel = view->xdg_surface->toplevel;

    /* ACK the initial configure so the client can start drawing */
    wlr_xdg_surface_schedule_configure(view->xdg_surface);

    /* We no longer need this listener — disconnect it and mark detached */
    wl_list_remove(&view->commit.link);
    wl_list_init(&view->commit.link);
}

/* =========================================================================
 * NEW XDG SURFACE
 * ========================================================================= */

void server_new_xdg_surface(struct wl_listener *listener, void *data) {
    struct tiny_server    *server      = wl_container_of(listener, server, new_xdg_surface);
    struct wlr_xdg_surface *xdg_surface = data;

    /* We only handle toplevels (normal windows).
     * Popups are managed entirely by wlroots — we must NOT create a view for
     * them.  Without this check, every right-click menu or tooltip would get
     * a view struct and crash when we try to tile it. */
    //if (xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL) return;

    struct tiny_view *view = calloc(1, sizeof(struct tiny_view));
    if (!view) return; /* OOM guard */

    view->server      = server;
    view->xdg_surface = xdg_surface;
    view->xdg_toplevel = xdg_surface->toplevel; /* may be NULL until first commit */

    /* Mark the link as detached so safety checks work before map fires */
    wl_list_init(&view->link);

    /* ---- Scene graph ---- */
    view->scene_tree = wlr_scene_xdg_surface_create(&server->scene->tree, xdg_surface);
    view->scene_tree->node.data = view; /* tag so hit-testing can find us */

    /* ---- Border rect (child of scene_tree → inherits window position) ----
     * Size 0×0 at creation; apply_tiling_to_view will resize it.
     * lower_to_bottom puts it behind the surface content so it shows as
     * an outline around the edges of the client. */
    float border_color[4] = {1.f, 0.f, 0.f, 1.f}; /* red — change to taste */
    view->border = wlr_scene_rect_create(view->scene_tree, 0, 0, border_color);
    wlr_scene_node_set_position(&view->border->node, 0, 0);
    wlr_scene_node_lower_to_bottom(&view->border->node);
    wlr_scene_node_set_enabled(&view->border->node, false); /* hidden until focused */

    /* ---- Listeners ---- */
    view->commit.notify = xdg_surface_handle_commit;
    wl_signal_add(&xdg_surface->surface->events.commit, &view->commit);

    view->map.notify = xdg_toplevel_map;
    wl_signal_add(&xdg_surface->surface->events.map, &view->map);

    view->unmap.notify = xdg_toplevel_unmap;
    wl_signal_add(&xdg_surface->surface->events.unmap, &view->unmap);

    view->destroy.notify = xdg_toplevel_destroy;
    wl_signal_add(&xdg_surface->events.destroy, &view->destroy);

    printf("XDG NEW SURFACE: waiting for initial commit...\n");
}

/* =========================================================================
 * LAYER SHELL  (e.g. Waybar)
 * ========================================================================= */

struct tiny_layer_surface {
    struct wlr_layer_surface_v1 *layer_surface;
    struct tiny_server          *server;
    struct wl_listener           commit;
    struct wl_listener           destroy;
};

static void handle_layer_shell_surface_destroy(struct wl_listener *listener,
                                                void *data) {
    struct tiny_layer_surface *tiny_layer =
        wl_container_of(listener, tiny_layer, destroy);

    wl_list_remove(&tiny_layer->commit.link);
    wl_list_remove(&tiny_layer->destroy.link);
    free(tiny_layer);
}

static void handle_layer_shell_surface_commit(struct wl_listener *listener,
                                               void *data) {
    struct tiny_layer_surface   *tiny_layer  =
        wl_container_of(listener, tiny_layer, commit);
    struct wlr_layer_surface_v1 *layer_surface = tiny_layer->layer_surface;
    struct tiny_server          *server        = tiny_layer->server;

    if (!layer_surface->initialized) return;

    struct wlr_scene_layer_surface_v1 *scene_surface = layer_surface->data;

    struct wlr_box full_area = {0};
    wlr_output_layout_get_box(server->output_layout,
                               layer_surface->output, &full_area);

    /* Update the usable screen area so tiled windows don't go under the bar */
    if (layer_surface->current.anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP) {
        uint32_t bar_h = layer_surface->current.exclusive_zone;
        server->usable_screen_area.x      = full_area.x;
        server->usable_screen_area.y      = full_area.y + bar_h;
        server->usable_screen_area.width  = full_area.width;
        server->usable_screen_area.height = full_area.height - bar_h;
    } else if (layer_surface->current.anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM) {
        uint32_t bar_h = layer_surface->current.exclusive_zone;
        server->usable_screen_area.height = full_area.height - bar_h;
    }

    wlr_scene_layer_surface_v1_configure(scene_surface, &full_area, &full_area);

    /* Send the initial configure ACK if not yet configured */
    if (!layer_surface->configured) {
        wlr_layer_surface_v1_configure(layer_surface,
            layer_surface->current.desired_width,
            layer_surface->current.desired_height);
    }
}

void server_new_layer_shell_surface(struct wl_listener *listener, void *data) {
    struct tiny_server          *server       =
        wl_container_of(listener, server, new_layer_shell_surface);
    struct wlr_layer_surface_v1 *layer_surface = data;

    struct tiny_layer_surface *tiny_layer =
        calloc(1, sizeof(struct tiny_layer_surface));
    if (!tiny_layer) return;

    tiny_layer->layer_surface = layer_surface;
    tiny_layer->server        = server;

    struct wlr_scene_layer_surface_v1 *scene_layer =
        wlr_scene_layer_surface_v1_create(&server->scene->tree, layer_surface);
    layer_surface->data = scene_layer;

    tiny_layer->commit.notify = handle_layer_shell_surface_commit;
    wl_signal_add(&layer_surface->surface->events.commit, &tiny_layer->commit);

    tiny_layer->destroy.notify = handle_layer_shell_surface_destroy;
    wl_signal_add(&layer_surface->events.destroy, &tiny_layer->destroy);
}
