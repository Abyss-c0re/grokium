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

INCLUDES := -Iinclude -I$(NANOBOT_ROOT)/src -I$(NANOBOT_ROOT)/include \
	-I$(NANOBOT_ROOT)/libs/sched -I$(NANOBOT_ROOT)/libs/provider
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

SRCS := src/grokium_main.c src/grokium_tui.c src/grokium_chat.c \
	src/grokium_config.c src/grokium_version.c src/grokium_hub.c \
	src/grokium_media.c src/grokium_status.c

.PHONY: all clean nanobot install test-status test-memory-seed test

all: nanobot out/grokium

nanobot:
	@test -f "$(NANOBOT_ROOT)/CMakeLists.txt" || \
	  (echo "missing nanobot at $(NANOBOT_ROOT) — run: ./scripts/sync_nanobot.sh"; exit 1)
	@if [ ! -f "$(NANOBOT_LIBDIR)/libnanobot_legacy.a" ]; then \
	  $(MAKE) -C "$(NANOBOT_ROOT)" host; \
	fi

out/grokium: $(SRCS) include/grokium.h include/grokium_chat.h include/grokium_config.h include/grokium_version.h include/grokium_hub.h include/grokium_media.h include/grokium_status.h
	@mkdir -p out
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LIBS)

# Dual-wire status probes only — no nanobot/ncurses link.
out/status_probe_selftest: tests/status_probe_selftest.c src/grokium_status.c \
	include/grokium_status.h
	@mkdir -p out
	$(CC) $(CFLAGS) -o $@ tests/status_probe_selftest.c src/grokium_status.c

# Resume memory seed: chat seed path + nanobot memory (no TUI/ncurses).
out/memory_seed_selftest: tests/memory_seed_selftest.c src/grokium_chat.c \
	include/grokium_chat.h nanobot
	@mkdir -p out
	$(CC) $(CFLAGS) -o $@ tests/memory_seed_selftest.c src/grokium_chat.c $(LIBS)

test-status: out/status_probe_selftest
	@./out/status_probe_selftest

test-memory-seed: out/memory_seed_selftest
	@./out/memory_seed_selftest

test: test-status test-memory-seed

clean:
	rm -f out/grokium out/status_probe_selftest out/memory_seed_selftest

install: all
	install -m755 out/grokium $(HOME)/.local/bin/grokium
