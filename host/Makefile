# Grokium host — links nanobot static core (embeddable peer stays separate).
CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L
ROOT := $(abspath ..)
NANOBOT_ROOT ?= $(ROOT)/deps/nanobot
# Sibling fallback for local dev
ifeq ($(wildcard $(NANOBOT_ROOT)/CMakeLists.txt),)
  ifneq ($(wildcard $(HOME)/Dev/AI/nanobot/CMakeLists.txt),)
    NANOBOT_ROOT := $(HOME)/Dev/AI/nanobot
  endif
endif
NANOBOT_BUILD ?= $(NANOBOT_ROOT)/build/host
NANOBOT_LIBDIR := $(NANOBOT_BUILD)/lib

C_CORE := $(ROOT)/c_core
INCLUDES := -Iinclude -I$(C_CORE)/include -I$(NANOBOT_ROOT)/src \
	-I$(NANOBOT_ROOT)/include -I$(NANOBOT_ROOT)/libs/sched \
	-I$(NANOBOT_ROOT)/libs/provider -I$(NANOBOT_ROOT)/libs/subagent
CFLAGS += $(INCLUDES)

LIBS := -L$(NANOBOT_LIBDIR) -Wl,--start-group \
	-lnanobot_legacy -lnanobot_provider -lnanobot_subagent -lnanobot_sched \
	-lnanobot_crypto -lnanobot_os -lnanobot_json \
	-Wl,--end-group -lpthread -lm -ldl -lncurses

# braincube optional
ifneq ($(wildcard $(NANOBOT_LIBDIR)/libbraincube.a),)
  LIBS := -L$(NANOBOT_LIBDIR) -Wl,--start-group \
	-lnanobot_legacy -lnanobot_provider -lnanobot_subagent -lnanobot_sched \
	-lnanobot_crypto -lnanobot_os -lnanobot_json -lbraincube \
	-Wl,--end-group -lpthread -lm -ldl -lncurses
endif

# Host-local SMX filter (coord/ingest sanitize before consolidate exec).
SMX_FILTER_SRC := $(C_CORE)/src/smx_filter.c $(C_CORE)/src/law.c \
	$(C_CORE)/src/smx.c $(C_CORE)/src/algocube.c $(C_CORE)/src/sha256.c

# Coord dual-wire plates (gk_coord_*); not linked into filter-only selftests.
CONSOLIDATOR_SRC := $(C_CORE)/src/consolidator.c

# Shared chat dual-wire deny plates (grokium_chat_err_json); not filter selftests.
LLAMA_SRC := $(C_CORE)/src/llama.c

SRCS := src/grokium_main.c src/grokium_tui.c src/grokium_chat.c \
	src/grokium_config.c src/grokium_version.c src/grokium_hub.c \
	src/grokium_media.c src/grokium_status.c src/grokium_plate.c \
	$(C_CORE)/src/session.c $(C_CORE)/src/status_plate.c \
	$(C_CORE)/src/fleet.c \
	$(SMX_FILTER_SRC) $(CONSOLIDATOR_SRC) $(LLAMA_SRC)

.PHONY: all clean nanobot install test-status test-version-compat \
	test-hub-status test-memory-seed test-media-plate test-session-plate \
	test-smx-host-filter test-plate-line test-settings-plate test

all: nanobot out/grokium

nanobot:
	@test -f "$(NANOBOT_ROOT)/CMakeLists.txt" || \
	  (echo "missing nanobot at $(NANOBOT_ROOT) — run: ./scripts/sync_nanobot.sh"; exit 1)
	@if [ ! -f "$(NANOBOT_LIBDIR)/libnanobot_legacy.a" ]; then \
	  $(MAKE) -C "$(NANOBOT_ROOT)" host; \
	fi

out/grokium: $(SRCS) include/grokium.h include/grokium_chat.h include/grokium_config.h include/grokium_version.h include/grokium_hub.h include/grokium_media.h include/grokium_status.h include/grokium_plate.h $(C_CORE)/include/grokium_session.h $(C_CORE)/include/grokium_smx_filter.h $(C_CORE)/include/grokium_law.h $(C_CORE)/include/grokium_consolidator.h $(C_CORE)/include/grokium_llama.h
	@mkdir -p out
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LIBS)

# Dual-wire status probes only — no nanobot/ncurses link (fleet via c_core).
out/status_probe_selftest: tests/status_probe_selftest.c src/grokium_status.c \
	include/grokium_status.h $(C_CORE)/src/status_plate.c \
	$(C_CORE)/src/fleet.c $(C_CORE)/include/grokium_status_plate.h \
	$(C_CORE)/include/grokium_fleet.h
	@mkdir -p out
	$(CC) $(CFLAGS) -o $@ tests/status_probe_selftest.c src/grokium_status.c \
		$(C_CORE)/src/status_plate.c $(C_CORE)/src/fleet.c

# Version compat plate dual-wire — no nanobot/ncurses link.
out/version_compat_selftest: tests/version_compat_selftest.c \
	src/grokium_version.c include/grokium_version.h include/grokium.h
	@mkdir -p out
	$(CC) $(CFLAGS) -o $@ tests/version_compat_selftest.c src/grokium_version.c

# Hub status dual-wire plate — no nanobot/ncurses link (may call curl for http).
out/hub_status_selftest: tests/hub_status_selftest.c src/grokium_hub.c \
	include/grokium_hub.h include/grokium.h include/grokium_config.h
	@mkdir -p out
	$(CC) $(CFLAGS) -o $@ tests/hub_status_selftest.c src/grokium_hub.c

# Resume memory seed: chat seed path + nanobot memory (no TUI/ncurses).
out/memory_seed_selftest: tests/memory_seed_selftest.c src/grokium_chat.c \
	include/grokium_chat.h nanobot
	@mkdir -p out
	$(CC) $(CFLAGS) -o $@ tests/memory_seed_selftest.c src/grokium_chat.c $(LIBS)

# Media mime + dual-wire honesty plate — no nanobot/ncurses link.
out/media_plate_selftest: tests/media_plate_selftest.c src/grokium_media.c \
	include/grokium_media.h include/grokium_config.h include/grokium.h
	@mkdir -p out
	$(CC) $(CFLAGS) -DGKX_MEDIA_NO_NANOBOT -o $@ \
		tests/media_plate_selftest.c src/grokium_media.c

# Session meta dual-wire honesty plates — shared pure-C c_core module.
out/session_plate_selftest: tests/session_plate_selftest.c \
	$(C_CORE)/src/session.c $(C_CORE)/include/grokium_session.h
	@mkdir -p out
	$(CC) $(CFLAGS) -o $@ tests/session_plate_selftest.c $(C_CORE)/src/session.c

test-status: out/status_probe_selftest
	@set -e; \
	ST=$$(./out/status_probe_selftest); \
	echo "$$ST" | grep -q '"schema":"grokium.status_probe_selftest.v1"'; \
	echo "$$ST" | grep -q '"ok":true'; \
	echo "$$ST" | grep -q '"fleet_n":6'; \
	echo "$$ST" | grep -q '"healthz":true'; \
	echo "$$ST" | grep -q '"product_wire":"smx2"'; \
	echo "$$ST" | grep -q '"python":0'; \
	echo "$$ST" | grep -qv HOST_STATUS_PROBE_OK

test-version-compat: out/version_compat_selftest
	@set -e; \
	ST=$$(./out/version_compat_selftest); \
	echo "$$ST" | grep -q '"schema":"grokium.version_compat_selftest.v1"'; \
	echo "$$ST" | grep -q '"ok":true'; \
	echo "$$ST" | grep -q '"reported":"9.9.9"'; \
	echo "$$ST" | grep -q '"restart_plate":true'; \
	echo "$$ST" | grep -q '"product_wire":"smx2"'; \
	echo "$$ST" | grep -q '"python":0'; \
	echo "$$ST" | grep -qv HOST_VERSION_COMPAT_OK

test-hub-status: out/hub_status_selftest
	@set -e; \
	ST=$$(./out/hub_status_selftest); \
	echo "$$ST" | grep -q '"schema":"grokium.hub_status_selftest.v1"'; \
	echo "$$ST" | grep -q '"ok":true'; \
	echo "$$ST" | grep -q '"peer_http":"lab_ops_only"'; \
	echo "$$ST" | grep -q '"lock_slots_sanitize":true'; \
	echo "$$ST" | grep -q '"product_wire":"smx2"'; \
	echo "$$ST" | grep -q '"python":0'; \
	echo "$$ST" | grep -qv HOST_HUB_STATUS_OK

test-memory-seed: out/memory_seed_selftest
	@set -e; \
	ST=$$(./out/memory_seed_selftest); \
	echo "$$ST" | grep -q '"schema":"grokium.memory_seed_selftest.v1"'; \
	echo "$$ST" | grep -q '"ok":true'; \
	echo "$$ST" | grep -q '"host_local":true'; \
	echo "$$ST" | grep -q '"product_bus":false'; \
	echo "$$ST" | grep -q '"workdir_bound":true'; \
	echo "$$ST" | grep -q '"python":0'; \
	echo "$$ST" | grep -qv HOST_MEMORY_SEED_OK

test-media-plate: out/media_plate_selftest
	@set -e; \
	ST=$$(./out/media_plate_selftest); \
	echo "$$ST" | grep -q '"schema":"grokium.media_plate_selftest.v1"'; \
	echo "$$ST" | grep -q '"ok":true'; \
	echo "$$ST" | grep -q '"mime":true'; \
	echo "$$ST" | grep -q '"viz":true'; \
	echo "$$ST" | grep -q '"product_wire":"smx2"'; \
	echo "$$ST" | grep -q '"python":0'; \
	echo "$$ST" | grep -qv HOST_MEDIA_PLATE_OK

test-session-plate: out/session_plate_selftest
	@set -e; \
	ST=$$(./out/session_plate_selftest); \
	echo "$$ST" | grep -q '"schema":"grokium.host_session_plate_selftest.v1"'; \
	echo "$$ST" | grep -q '"ok":true'; \
	echo "$$ST" | grep -q '"shared":"c_core"'; \
	echo "$$ST" | grep -q '"id_safe":true'; \
	echo "$$ST" | grep -q '"product_wire":"smx2"'; \
	echo "$$ST" | grep -q '"python":0'; \
	echo "$$ST" | grep -qv HOST_SESSION_PLATE_OK

# Host-local SMX filter gate — pure C, no nanobot/ncurses.
out/smx_host_filter_selftest: tests/smx_host_filter_selftest.c $(SMX_FILTER_SRC) \
	$(C_CORE)/include/grokium_smx_filter.h $(C_CORE)/include/grokium_law.h
	@mkdir -p out
	$(CC) $(CFLAGS) -o $@ tests/smx_host_filter_selftest.c $(SMX_FILTER_SRC)

test-smx-host-filter: out/smx_host_filter_selftest
	@set -e; \
	ST=$$(./out/smx_host_filter_selftest); \
	echo "$$ST" | grep -q '"schema":"grokium.smx_host_filter_selftest.v1"'; \
	echo "$$ST" | grep -q '"ok":true'; \
	echo "$$ST" | grep -q '"external":"strict"'; \
	echo "$$ST" | grep -q '"hold_flash":1'; \
	echo "$$ST" | grep -q '"product_wire":"smx2"'; \
	echo "$$ST" | grep -q '"python":0'; \
	echo "$$ST" | grep -qv HOST_SMX_FILTER_OK

# TUI tool-capture plate-line filter — pure C, no nanobot/ncurses.
out/plate_line_selftest: tests/plate_line_selftest.c src/grokium_plate.c \
	include/grokium_plate.h
	@mkdir -p out
	$(CC) $(CFLAGS) -o $@ tests/plate_line_selftest.c src/grokium_plate.c

test-plate-line: out/plate_line_selftest
	@set -e; \
	ST=$$(./out/plate_line_selftest); \
	echo "$$ST" | grep -q '"schema":"grokium.plate_line_selftest.v1"'; \
	echo "$$ST" | grep -q '"ok":true'; \
	echo "$$ST" | grep -q '"dual_wire":"keep"'; \
	echo "$$ST" | grep -q '"free_json":"drop"'; \
	echo "$$ST" | grep -q '"product_wire":"smx2"'; \
	echo "$$ST" | grep -q '"python":0'; \
	echo "$$ST" | grep -qv HOST_PLATE_LINE_OK

# Dual-wire settings plate — pure C, no nanobot/ncurses.
out/settings_plate_selftest: tests/settings_plate_selftest.c src/grokium_config.c \
	include/grokium_config.h
	@mkdir -p out
	$(CC) $(CFLAGS) -DGKX_CONFIG_NO_NCURSES -o $@ \
		tests/settings_plate_selftest.c src/grokium_config.c

test-settings-plate: out/settings_plate_selftest
	@set -e; \
	ST=$$(./out/settings_plate_selftest); \
	echo "$$ST" | grep -q '"schema":"grokium.settings_plate_selftest.v1"'; \
	echo "$$ST" | grep -q '"ok":true'; \
	echo "$$ST" | grep -q '"empty_output":true'; \
	echo "$$ST" | grep -q '"ready":true'; \
	echo "$$ST" | grep -q '"product_wire":"smx2"'; \
	echo "$$ST" | grep -q '"python":0'; \
	echo "$$ST" | grep -qv HOST_SETTINGS_PLATE_OK

test: test-status test-version-compat test-hub-status test-memory-seed \
	test-media-plate test-session-plate test-smx-host-filter test-plate-line \
	test-settings-plate

clean:
	rm -f out/grokium out/status_probe_selftest out/version_compat_selftest \
		out/hub_status_selftest out/memory_seed_selftest out/media_plate_selftest \
		out/session_plate_selftest out/smx_host_filter_selftest \
		out/plate_line_selftest out/settings_plate_selftest

install: all
	install -m755 out/grokium $(HOME)/.local/bin/grokium
