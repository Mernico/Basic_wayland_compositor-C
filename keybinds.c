#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <linux/input-event-codes.h>
#include <wlr/types/wlr_keyboard.h>
#include <xkbcommon/xkbcommon.h>
#include "server.h"

/* =========================================================================
 * SPAWN HELPER
 * ========================================================================= */
static void spawn(const char *cmd) {
    pid_t pid = fork();
    if (pid < 0) return;
    if (pid > 0) return; /* Parent */

    /* Child process */
    unsetenv("DISPLAY");
    setenv("WAYLAND_DISPLAY", "wayland-1", 1);
    
    /* Using execlp: first arg is the file, second is argv[0] */
    execlp(cmd, cmd, (char *)NULL);
    _exit(1); 
}

/* =========================================================================
 * CLOSE FOCUSED VIEW
 * Removed 'static' to match server.h
 * ========================================================================= */


/* =========================================================================
 * SUPER + ARROWS (Move/Resize)
 * ========================================================================= */
static bool handle_super_arrow(struct tiny_server *server, 
                                xkb_keysym_t sym, bool shifted) {
    int dir = -1;
    switch (sym) {
        case XKB_KEY_Left:  dir = 0; break;
        case XKB_KEY_Right: dir = 1; break;
        case XKB_KEY_Up:    dir = 2; break;
        case XKB_KEY_Down:  dir = 3; break;
        default: return false;
    }

    if (shifted) {
        /* Resize logic: Right/Down = positive delta, Left/Up = negative */
        int32_t delta = (dir == 1 || dir == 3) ? 50 : -50;
        int axis = (dir <= 1) ? 0 : 1; /* 0 = Horizontal, 1 = Vertical */
        resize_focused_window(server, axis, delta);
    } else {
        move_focused_window(server, dir);
    }
    return true;
}

/* =========================================================================
 * KEYBOARD HANDLE KEY
 * ========================================================================= */
void keyboard_handle_key(struct wl_listener *listener, void *data) {
    struct tiny_keyboard *keyboard = wl_container_of(listener, keyboard, key);
    struct wlr_keyboard_key_event *event = data;
    struct tiny_server *server = keyboard->server;

    xkb_keycode_t keycode = event->keycode + 8;
    const xkb_keysym_t *syms;
    int nsyms = xkb_state_key_get_syms(
        keyboard->wlr_keyboard->xkb_state, keycode, &syms);

    uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);
    bool shifted = modifiers & WLR_MODIFIER_SHIFT;
    bool consumed = false;

    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        for (int i = 0; i < nsyms && !consumed; i++) {
            xkb_keysym_t sym = syms[i];

            /* Alt + Keys */
            if (modifiers & WLR_MODIFIER_ALT) {
                switch (sym) {
                    case XKB_KEY_Escape: 
                        wl_display_terminate(server->wl_display); 
                        consumed = true; 
                        break;
                    case XKB_KEY_w: spawn("epiphany"); consumed = true; break;
                    case XKB_KEY_q: spawn("kitty"); consumed = true; break;
                    case XKB_KEY_e: spawn("waybar"); consumed = true; break;
                    case XKB_KEY_r: spawn("dolphin"); consumed = true; break;
                    case XKB_KEY_c: 
                        close_focused_view(server); 
                        consumed = true; 
                        break;
                }
            }

            /* Super + Arrows */
            if (!consumed && (modifiers & WLR_MODIFIER_ALT)) {
                consumed = handle_super_arrow(server, sym, shifted);
            }
        }
    }

    if (!consumed) {
        wlr_seat_keyboard_notify_key(server->seat, 
            event->time_msec, event->keycode, event->state);
    }
}


static void ignore_sigchld(void) {
    struct sigaction sa = {0};
    sa.sa_handler = SIG_IGN;
    sa.sa_flags   = SA_NOCLDWAIT;
    sigaction(SIGCHLD, &sa, NULL);
}
 
void autostart(struct tiny_server *server) {
    (void)server;
 
    ignore_sigchld();
 
    spawn("waybar");
}
