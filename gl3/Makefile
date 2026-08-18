PKG_CONFIG ?= pkg-config
WAYLAND_SCANNER ?= wayland-scanner

PKGS = wlroots-0.20 wayland-server xkbcommon libjpeg
CFLAGS_PKG != $(PKG_CONFIG) --cflags $(PKGS)
LIBS != $(PKG_CONFIG) --libs $(PKGS)

# where gl3 looks for its background image at runtime, override for installs
DATADIR ?= $(CURDIR)/assets

# wlroots doesn't install a header for wlr-layer-shell-v1 (it generates its
# own copy privately at build time and keeps it to itself), so gl3 generates
# its own from the protocol xml, same as wlroots itself does internally
WLR_PROTOCOLS_DATADIR != $(PKG_CONFIG) --variable=pkgdatadir wlr-protocols
GENERATED_DIR = generated
GENERATED_HEADER = $(GENERATED_DIR)/wlr-layer-shell-unstable-v1-protocol.h

CFLAGS += $(CFLAGS_PKG) -I$(GENERATED_DIR) -DWLR_USE_UNSTABLE -DGL3_DATADIR=\"$(DATADIR)\" -Wall -g

all: gl3

$(GENERATED_HEADER): $(WLR_PROTOCOLS_DATADIR)/unstable/wlr-layer-shell-unstable-v1.xml
	mkdir -p $(GENERATED_DIR)
	$(WAYLAND_SCANNER) server-header $< $@

gl3: src/gl3.c $(GENERATED_HEADER)
	$(CC) $< $(CFLAGS) $(LDFLAGS) $(LIBS) -o $@

clean:
	rm -f gl3
	rm -rf $(GENERATED_DIR)

.PHONY: all clean
