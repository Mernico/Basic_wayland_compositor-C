CC = gcc

# Added wayland-server and wayland-client to the pkg-config calls
CFLAGS = -Wall -Wextra -std=c11 -DWLR_USE_UNSTABLE \
    $(shell pkg-config --cflags wlroots-0.19 wayland-server wayland-client)

LDFLAGS = $(shell pkg-config --libs wlroots-0.19 wayland-server wayland-client)

all:
	$(CC) main.c -o waycomp $(CFLAGS) $(LDFLAGS)

clean:
	rm -f waycomp
