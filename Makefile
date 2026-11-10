# Thin wrapper so `make` works the same here as in the sibling Haiku ports.
# The real build system is meson: each vendored library under lv2stack/ builds
# with its own unmodified meson.build into a shared static stage prefix
# (build/stage), chained via PKG_CONFIG_PATH in dependency order
# lv2 -> zix -> serd -> sord -> sratom -> lilv. Our own hosts and plugin are
# small enough to compile with direct rules against the stage.
#
# The hosts (lv2host, lv2guihost) play through Kasen's audio backends,
# copied into jdbackend/ by tools/sync-jdbackend.sh: the Media Kit built in,
# JACK as the jack.so add-on (built here when JACK's headers are found). Their
# libraries are staged for apps built on this stack (rktuner):
#   build/stage/lib/liblv2host.a      the hosting core and the UI host
#   build/stage/lib/libjdbackend.a    the Media Kit backend and the loader
#   build/stage/lib/jdbackend/jack.so the JACK add-on
#   build/stage/develop/headers/lv2host/  their headers
#
# Typical loop from the dev host:
#   rsync -a --delete --exclude build --exclude .git ./ haikulaptop:LV2-renku/
#   ssh haikulaptop 'cd LV2-renku && make'

BUILD_DIR ?= build
STAGE := $(abspath $(BUILD_DIR)/stage)
LV2_INSTALL_DIR ?= $(HOME)/config/non-packaged/add-ons/media/LV2

# Haiku-native default plugin search path (mirrors the sibling VST3 port's
# add-ons convention); set at build time through lilv's default_lv2_path
# option so no upstream file is patched. LV2_PATH at runtime still overrides.
DEFAULT_LV2_PATH ?= /boot/home/config/non-packaged/add-ons/media/LV2:/boot/system/non-packaged/add-ons/media/LV2:/boot/system/add-ons/media/LV2

MESON ?= meson
NINJA ?= ninja
PKGCFG := PKG_CONFIG_PATH=$(STAGE)/lib/pkgconfig

MESON_COMMON = --prefix=$(STAGE) --libdir=lib --default-library=static \
	--buildtype=release -Ddocs=disabled -Dlint=false -Dtests=disabled

# Per-library options (verified against each lv2stack/<lib>/meson_options.txt).
# lv2: old_headers installs the <lv2/...> URI-style include layout the hosts
# use; lv2dir is set explicitly because upstream's Haiku default is the
# obsolete pre-package-management /boot/common/add-ons/media/LV2.
MESON_OPTS_lv2 = -Dtools=disabled -Dold_headers=true -Dbundles=true \
	-Dlv2dir=$(STAGE)/lib/lv2
MESON_OPTS_zix = -Dtests_cpp=disabled -Dbenchmarks=disabled
MESON_OPTS_serd = -Dtools=disabled -Dman=disabled
MESON_OPTS_sord = -Dtools=disabled -Dman=disabled -Dbindings_cpp=disabled
MESON_OPTS_sratom =
MESON_OPTS_lilv = -Dtools=enabled -Dbindings_py=disabled -Dbindings_cpp=disabled \
	-Ddynmanifest=disabled "-Ddefault_lv2_path=$(DEFAULT_LV2_PATH)"

CC ?= gcc
CXX ?= g++
WARN = -Wall -Wextra
UNAME := $(shell uname)

# JACK's headers, only for the jack.so add-on (nothing else includes them):
# the sibling JACK2 port tree (the exact source of the installed libjack),
# else the non-packaged or system headers. Without them there is no add-on
# and the hosts run on the Media Kit only.
JACK_PORT_DIR ?= $(HOME)/jack-port-renku
JACK_INC ?= $(firstword $(foreach d,$(JACK_PORT_DIR)/common /boot/system/non-packaged/include \
	/boot/system/develop/headers,$(if $(wildcard $(d)/jack/jack.h),$(d))))
JACK_CFLAGS ?= $(if $(JACK_INC),-I$(JACK_INC))
JACK_LIBS ?= -L/boot/system/non-packaged/lib -ljack

HKAMP_SRC = plugins/hkamp.lv2
HKAMP_OUT = $(BUILD_DIR)/hkamp.lv2

all: build

HKAMP_TTLS = $(HKAMP_OUT)/manifest.ttl $(HKAMP_OUT)/hkamp.ttl

JACK_ADDON = $(if $(JACK_CFLAGS),$(BUILD_DIR)/bin/backends/jack.so)

ifeq ($(UNAME),Haiku)
# The hosts, the plugin UI and the backends need the Haiku kits.
build: stack $(BUILD_DIR)/bin/lv2host $(BUILD_DIR)/bin/lv2guihost $(JACK_ADDON) stage-dev \
	$(HKAMP_OUT)/hkamp.so $(HKAMP_OUT)/hkamp_ui.so $(HKAMP_TTLS)
else
build: stack $(HKAMP_OUT)/hkamp.so $(HKAMP_TTLS)
endif

# ---- vendored stack (meson, unmodified upstream) --------------------------

stack: $(BUILD_DIR)/.stamp-lilv

$(BUILD_DIR)/.stamp-lv2:
$(BUILD_DIR)/.stamp-zix: $(BUILD_DIR)/.stamp-lv2
$(BUILD_DIR)/.stamp-serd: $(BUILD_DIR)/.stamp-zix
$(BUILD_DIR)/.stamp-sord: $(BUILD_DIR)/.stamp-serd
$(BUILD_DIR)/.stamp-sratom: $(BUILD_DIR)/.stamp-sord
$(BUILD_DIR)/.stamp-lilv: $(BUILD_DIR)/.stamp-sratom

$(BUILD_DIR)/.stamp-%:
	@if [ ! -f $(BUILD_DIR)/$*/build.ninja ]; then \
		$(PKGCFG) $(MESON) setup $(BUILD_DIR)/$* lv2stack/$* \
			$(MESON_COMMON) $(MESON_OPTS_$*); \
	fi
	$(PKGCFG) $(MESON) install -C $(BUILD_DIR)/$*
	@touch $@

# ---- our own code (direct rules against the stage) ------------------------

JD_DIR = jdbackend
LV2_CFLAGS = `$(PKGCFG) pkg-config --cflags lilv-0 zix-0 lv2`
LV2_LIBS = `$(PKGCFG) pkg-config --static --libs lilv-0 zix-0`
HOST_LIBS = -lbe -lmedia -lmidi2 -lsamplerate

# Kasen's backends, compiled as Kasen compiles them.
JD_OBJS = $(BUILD_DIR)/jd/mediakit_backend.o $(BUILD_DIR)/jd/audio_backend.o \
	$(BUILD_DIR)/jd/rt_ringbuffer.o
JD_LIB = $(BUILD_DIR)/libjdbackend.a

$(BUILD_DIR)/jd/mediakit_backend.o: $(JD_DIR)/backend/mediakit/mediakit_backend.cpp \
		$(JD_DIR)/engine/audio_backend.h $(JD_DIR)/engine/rt_ringbuffer.h
	@mkdir -p $(BUILD_DIR)/jd
	$(CXX) -std=c++17 $(WARN) -O2 -g -fPIC -I$(JD_DIR) -c -o $@ $<

$(BUILD_DIR)/jd/%.o: $(JD_DIR)/engine/%.c $(JD_DIR)/engine/%.h
	@mkdir -p $(BUILD_DIR)/jd
	$(CC) -std=gnu99 $(WARN) -O2 -g -fPIC -I$(JD_DIR) -c -o $@ $<

$(JD_LIB): $(JD_OBJS)
	rm -f $@
	ar rcs $@ $^

# The JACK add-on: dlopened by the loader, the only thing that links libjack.
$(BUILD_DIR)/bin/backends/jack.so: $(JD_DIR)/backend/jack/jack_backend.c \
		$(JD_DIR)/engine/audio_backend.h
	@mkdir -p $(BUILD_DIR)/bin/backends
	$(CC) -std=gnu99 $(WARN) -O2 -fPIC -shared -I$(JD_DIR) $(JACK_CFLAGS) -o $@ $< $(JACK_LIBS)

# The hosting core (C) and the UI host (C++).
CORE_HDR = hosts/common/lv2host_core.h
UI_HDR = hosts/common/lv2ui_host.h
CORE_OBJ = $(BUILD_DIR)/lv2host_core.o
UI_OBJ = $(BUILD_DIR)/lv2ui_host.o
HOST_LIB = $(BUILD_DIR)/liblv2host.a

$(CORE_OBJ): hosts/common/lv2host_core.c $(CORE_HDR) $(JD_DIR)/engine/audio_backend.h \
		$(BUILD_DIR)/.stamp-lilv
	@mkdir -p $(BUILD_DIR)
	$(CC) -std=c99 $(WARN) -O2 -g -fPIC -I$(JD_DIR) -c -o $@ $< $(LV2_CFLAGS)

$(UI_OBJ): hosts/common/lv2ui_host.cpp $(UI_HDR) $(CORE_HDR) ns/ui-haiku.lv2/ui_haiku.h \
		$(BUILD_DIR)/.stamp-lilv
	@mkdir -p $(BUILD_DIR)
	$(CXX) -std=c++17 $(WARN) -O2 -g -fPIC -I$(JD_DIR) -c -o $@ $< $(LV2_CFLAGS)

$(HOST_LIB): $(CORE_OBJ) $(UI_OBJ)
	rm -f $@
	ar rcs $@ $^

$(BUILD_DIR)/bin/lv2host: hosts/lv2host/lv2host.cpp $(CORE_HDR) $(HOST_LIB) $(JD_LIB)
	@mkdir -p $(BUILD_DIR)/bin
	$(CXX) -std=c++17 $(WARN) -O2 -g -I$(JD_DIR) -o $@ $< $(LV2_CFLAGS) \
		$(HOST_LIB) $(JD_LIB) $(LV2_LIBS) $(HOST_LIBS)

$(BUILD_DIR)/bin/lv2guihost: hosts/lv2guihost/lv2guihost.cpp $(CORE_HDR) $(UI_HDR) \
		$(HOST_LIB) $(JD_LIB)
	@mkdir -p $(BUILD_DIR)/bin
	$(CXX) -std=c++17 $(WARN) -O2 -g -I$(JD_DIR) -o $@ $< $(LV2_CFLAGS) \
		$(HOST_LIB) $(JD_LIB) $(LV2_LIBS) $(HOST_LIBS)

# Stage the host libraries, their headers and the JACK add-on for apps built
# on this stack (rktuner): link liblv2host.a, libjdbackend.a and the lilv
# stack with -lbe -lmedia -lmidi2 -lsamplerate, include <lv2host/lv2host_core.h>.
STAGE_HDRS = $(STAGE)/develop/headers/lv2host
stage-dev: $(HOST_LIB) $(JD_LIB) $(JACK_ADDON)
	mkdir -p $(STAGE)/lib $(STAGE_HDRS)/engine
	cp $(HOST_LIB) $(JD_LIB) $(STAGE)/lib/
	cp $(CORE_HDR) $(UI_HDR) $(STAGE_HDRS)/
	cp $(JD_DIR)/engine/audio_backend.h $(STAGE_HDRS)/engine/
	@if [ -n "$(JACK_ADDON)" ]; then \
		mkdir -p $(STAGE)/lib/jdbackend; cp $(JACK_ADDON) $(STAGE)/lib/jdbackend/; \
	fi

$(HKAMP_OUT)/hkamp.so: $(HKAMP_SRC)/hkamp.c $(BUILD_DIR)/.stamp-lv2
	@mkdir -p $(HKAMP_OUT)
	$(CC) -std=c99 $(WARN) -O2 -fPIC -shared -o $@ $< \
		`$(PKGCFG) pkg-config --cflags lv2` -lm

$(HKAMP_OUT)/%.ttl: $(HKAMP_SRC)/%.ttl
	@mkdir -p $(HKAMP_OUT)
	cp $< $@

# Haiku only: the plugin's native BView UI (second .so in the bundle).
$(HKAMP_OUT)/hkamp_ui.so: $(HKAMP_SRC)/hkamp_ui.cpp $(BUILD_DIR)/.stamp-lv2
	@mkdir -p $(HKAMP_OUT)
	$(CXX) -std=c++17 $(WARN) -O2 -fPIC -shared -o $@ $< \
		`$(PKGCFG) pkg-config --cflags lv2` -lbe

# ---- install / validate ----------------------------------------------------

# On Haiku only: install build prerequisites.
deps:
	pkgman install -y meson ninja pkgconfig

# The LV2 specification bundles must be discoverable on LV2_PATH for lilv to
# resolve plugin metadata. ns/ui-haiku.lv2 (the HaikuUI type) installs with
# them so UIs declaring the type resolve too.
install-specs: stack
	mkdir -p $(LV2_INSTALL_DIR)
	@for b in $(STAGE)/lib/lv2/*.lv2 ns/ui-haiku.lv2; do \
		[ -d "$$b" ] || continue; \
		echo "install $$b -> $(LV2_INSTALL_DIR)/$$(basename $$b)"; \
		rm -rf "$(LV2_INSTALL_DIR)/$$(basename $$b)"; \
		cp -r "$$b" "$(LV2_INSTALL_DIR)/"; \
	done

install: build
	mkdir -p $(LV2_INSTALL_DIR)
	rm -rf $(LV2_INSTALL_DIR)/hkamp.lv2
	cp -r $(HKAMP_OUT) $(LV2_INSTALL_DIR)/hkamp.lv2

validate: stack
	@echo "== lv2ls =="
	$(STAGE)/bin/lv2ls
	@echo "== lv2info hkamp =="
	$(STAGE)/bin/lv2info https://github.com/rations/LV2-haiku/plugins/hkamp

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all build stack stage-dev deps install install-specs validate clean
