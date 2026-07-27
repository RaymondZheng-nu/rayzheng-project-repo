PKG_CONFIG ?= pkg-config

PKGS = wlroots-0.20 wayland-server xkbcommon
CFLAGS_PKG != $(PKG_CONFIG) --cflags $(PKGS)
LIBS != $(PKG_CONFIG) --libs $(PKGS)

CFLAGS += $(CFLAGS_PKG) -DWLR_USE_UNSTABLE -Wall -g

all: gl3

gl3: src/gl3.c
	$(CC) $< $(CFLAGS) $(LDFLAGS) $(LIBS) -o $@

clean:
	rm -f gl3

.PHONY: all clean
