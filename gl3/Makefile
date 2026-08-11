PKG_CONFIG ?= pkg-config

PKGS = wlroots-0.20 wayland-server xkbcommon libjpeg
CFLAGS_PKG != $(PKG_CONFIG) --cflags $(PKGS)
LIBS != $(PKG_CONFIG) --libs $(PKGS)

# where gl3 looks for its background image at runtime, override for installs
DATADIR ?= $(CURDIR)/assets

CFLAGS += $(CFLAGS_PKG) -DWLR_USE_UNSTABLE -DGL3_DATADIR=\"$(DATADIR)\" -Wall -g

all: gl3

gl3: src/gl3.c
	$(CC) $< $(CFLAGS) $(LDFLAGS) $(LIBS) -o $@

clean:
	rm -f gl3

.PHONY: all clean
