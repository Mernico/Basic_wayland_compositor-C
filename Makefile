CC = gcc
PKGS = wlroots-0.19 wayland-server xkbcommon pixman-1

WAYLAND_PROTOCOLS = $(shell pkg-config --variable=pkgdatadir wayland-protocols)
WAYLAND_SCANNER = $(shell pkg-config --variable=wayland_scanner wayland-scanner)

CFLAGS = -Wall -Wextra -std=c11 -DWLR_USE_UNSTABLE $(shell pkg-config --cflags $(PKGS)) -I.
LDFLAGS = $(shell pkg-config --libs $(PKGS))

# 1. Added the generated layer-shell .c file to the source list
SRCS = main.c input.c output.c xdg_shell.c xdg-shell-protocol.c \
       wlr-layer-shell-unstable-v1-protocol.c window_rules.c keybinds.c
OBJS = $(SRCS:.c=.o)

all: waycomp

# Protocol generation: XDG Shell
xdg-shell-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		$(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@

xdg-shell-protocol.c: xdg-shell-protocol.h
	$(WAYLAND_SCANNER) private-code \
		$(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@

# 2. NEW: Protocol generation: Layer Shell (Using the wlr-protocols path)
wlr-layer-shell-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		/usr/share/wlr-protocols/unstable/wlr-layer-shell-unstable-v1.xml $@

wlr-layer-shell-unstable-v1-protocol.c: wlr-layer-shell-unstable-v1-protocol.h
	$(WAYLAND_SCANNER) private-code \
		/usr/share/wlr-protocols/unstable/wlr-layer-shell-unstable-v1.xml $@

# Link the final binary
waycomp: $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

# 3. Updated dependencies so .o files wait for BOTH headers to be generated
%.o: %.c server.h xdg-shell-protocol.h wlr-layer-shell-unstable-v1-protocol.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f waycomp *.o xdg-shell-protocol.c xdg-shell-protocol.h \
	wlr-layer-shell-unstable-v1-protocol.c wlr-layer-shell-unstable-v1-protocol.h

.PHONY: all clean
