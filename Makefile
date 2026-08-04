# Grokium — nanobot core + C host (+ optional CubalC board). No Python.
ROOT := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
CUBALC_ROOT ?= $(ROOT)deps/cubalc
NANOBOT_ROOT ?= $(ROOT)deps/nanobot
BRAINCUBE_ROOT ?= $(ROOT)deps/braincube
.PHONY: all cubalc nanobot braincube host test install clean sync-cubalc sync-nanobot sync-braincube

all: braincube nanobot host

braincube:
	@bash scripts/sync_braincube.sh

nanobot: braincube
	@bash scripts/sync_nanobot.sh
	@test -f "$(NANOBOT_ROOT)/CMakeLists.txt" || test -f "$(HOME)/Dev/AI/nanobot/CMakeLists.txt" || \
	  (echo "missing nanobot — ./scripts/sync_nanobot.sh"; exit 1)
	$(MAKE) -C host nanobot

cubalc:
	@test -d "$(CUBALC_ROOT)" || (echo "missing $(CUBALC_ROOT) — run: ./scripts/sync_cubalc.sh"; exit 1)
	$(MAKE) -C $(CUBALC_ROOT) all

host: nanobot
	$(MAKE) -C host all

test: host
	@bash scripts/test.sh

install: all
	@bash scripts/install.sh

sync-cubalc:
	@bash scripts/sync_cubalc.sh

sync-nanobot:
	@bash scripts/sync_nanobot.sh

sync-braincube:
	@bash scripts/sync_braincube.sh

clean:
	$(MAKE) -C host clean
	rm -rf data/cubalc
