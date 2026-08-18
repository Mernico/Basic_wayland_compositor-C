#include <stdlib.h>
#include <unistd.h>
#include <linux/input-event-codes.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <xkbcommon/xkbcommon.h>
#include "server.h"

/* =========================================================================
 * CURSOR
 * ========================================================================= */

void server_cursor_motion(struct wl_listener *listener, void *data) {
    struct tiny_server *server = wl_container_of(listener, server, cursor_motion);
    struct wlr_pointer_motion_event *event = data;

    wlr_cursor_move(server->cursor, &event->pointer->base,
                    event->delta_x, event->delta_y);

    /* Update which view the mouse is over, then forward pointer position
     * to whatever surface currently has pointer focus. */
    event_mouse_hover_over_client(server);

    /* Notify the focused surface of the new pointer position.
     * Without this, the client never receives cursor-move events. */
    if (server->mouse_over_view) {
        struct wlr_xdg_surface *xdg = server->mouse_over_view->xdg_surface;
        double sx = server->cursor->x - server->mouse_over_view->x;
        double sy = server->cursor->y - server->mouse_over_view->y;
        wlr_seat_pointer_notify_enter(server->seat, xdg->surface, sx, sy);
        wlr_seat_pointer_notify_motion(server->seat, event->time_msec, sx, sy);
    } else {
        wlr_seat_pointer_clear_focus(server->seat);
    }
}

void server_cursor_motion_absolute(struct wl_listener *listener, void *data) {
    struct tiny_server *server =
        wl_container_of(listener, server, cursor_motion_absolute);
    struct wlr_pointer_motion_absolute_event *event = data;

    wlr_cursor_warp_absolute(server->cursor, &event->pointer->base,
                             event->x, event->y);

    /* Same hover + notify logic as relative motion */
    event_mouse_hover_over_client(server);

    if (server->mouse_over_view) {
        struct wlr_xdg_surface *xdg = server->mouse_over_view->xdg_surface;
        double sx = server->cursor->x - server->mouse_over_view->x;
        double sy = server->cursor->y - server->mouse_over_view->y;
        wlr_seat_pointer_notify_enter(server->seat, xdg->surface, sx, sy);
        wlr_seat_pointer_notify_motion(server->seat, event->time_msec, sx, sy);
    } else {
        wlr_seat_pointer_clear_focus(server->seat);
    }
}

/* =========================================================================
 * KEYBOARD — key event
 *
 * THE MAIN BUG FIX:
 * The old code forwarded raw key events to the seat but never told it about
 * modifier state (Shift, CapsLock, Ctrl, Alt, …).  XKB tracks modifiers
 * internally in wlr_keyboard, but the seat only knows about them if you
 * explicitly call wlr_seat_keyboard_notify_modifiers() — which requires a
 * separate "modifiers" listener on wlr_keyboard->events.modifiers.
 *
 * Without that listener:
 *   - Shift is ignored → lowercase only
 *   - CapsLock has no effect
 *   - Ctrl/Alt shortcuts don't work in apps
 *   - Compose sequences never fire
 * ========================================================================= */



/* keyboard_handle_key is defined in keybinds.c */

/* THE MISSING LISTENER:
 * This is called every time a modifier key changes (Shift pressed/released,
 * CapsLock toggled, etc.).  Without it, the seat's modifier state is never
 * updated and apps receive keys with no modifier information at all. */
static void keyboard_handle_modifiers(struct wl_listener *listener, void *data) {
    (void)data; /* modifier state is read from wlr_keyboard directly */
    struct tiny_keyboard *keyboard =
        wl_container_of(listener, keyboard, modifiers);

    /* Make this keyboard the seat's active keyboard, then push the updated
     * modifier state to whichever surface currently has keyboard focus. */
    wlr_seat_set_keyboard(keyboard->server->seat, keyboard->wlr_keyboard);
    wlr_seat_keyboard_notify_modifiers(keyboard->server->seat,
                                       &keyboard->wlr_keyboard->modifiers);
}

/* =========================================================================
 * NEW INPUT DEVICE
 * ========================================================================= */

void server_new_input(struct wl_listener *listener, void *data) {
    struct tiny_server      *server = wl_container_of(listener, server, new_input);
    struct wlr_input_device *device = data;

    if (device->type == WLR_INPUT_DEVICE_KEYBOARD) {
        struct tiny_keyboard *keyboard = calloc(1, sizeof(struct tiny_keyboard));
        if (!keyboard) return;

        keyboard->server      = server;
        keyboard->wlr_keyboard = wlr_keyboard_from_input_device(device);

        /* Build a keymap from the system locale/layout.
         * Passing NULL names uses the XKB_DEFAULT_* environment variables
         * (XKB_DEFAULT_LAYOUT, XKB_DEFAULT_VARIANT, etc.) so the user's
         * configured layout (AZERTY, Dvorak, …) is respected. */
        struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        struct xkb_keymap  *keymap  =
            xkb_keymap_new_from_names(context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);

        wlr_keyboard_set_keymap(keyboard->wlr_keyboard, keymap);
        xkb_keymap_unref(keymap);
        xkb_context_unref(context);

        /* Set a comfortable repeat rate: 25 keys/sec after 600 ms delay */
        wlr_keyboard_set_repeat_info(keyboard->wlr_keyboard, 25, 600);

        /* Key events */
        keyboard->key.notify = keyboard_handle_key;
        wl_signal_add(&keyboard->wlr_keyboard->events.key, &keyboard->key);

        /* Modifier events — THIS WAS MISSING and caused the Shift/CapsLock bug */
        keyboard->modifiers.notify = keyboard_handle_modifiers;
        wl_signal_add(&keyboard->wlr_keyboard->events.modifiers,
                      &keyboard->modifiers);

        wl_list_insert(&server->keyboards, &keyboard->link);
        wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);

    } else if (device->type == WLR_INPUT_DEVICE_POINTER) {
        wlr_cursor_attach_input_device(server->cursor, device);
    }

    uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
    if (!wl_list_empty(&server->keyboards))
        caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    wlr_seat_set_capabilities(server->seat, caps);
}

/* =========================================================================
 * MOUSE BUTTON — focus on click
 * ========================================================================= */

void window_focus_on_click(struct wl_listener *listener, void *data) {
    struct tiny_server *server = wl_container_of(listener, server, cursor_button);
    struct wlr_pointer_button_event *event = data;

    /* Always notify the seat first — the client needs the raw button event
     * regardless of whether we also change focus. */
    wlr_seat_pointer_notify_button(server->seat,
                                   event->time_msec,
                                   event->button,
                                   event->state);

    if (event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
        if (server->mouse_over_view)
            focus_view(server->mouse_over_view,
                       server->mouse_over_client_surface);
    }
}
