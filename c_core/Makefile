# Grokium C core — commander law + (expanding) smx/consolidator
CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11 -Iinclude
LDFLAGS ?= -lsodium

BUILD ?= ../build
BIN := $(BUILD)/grokium-commander

.PHONY: all clean test-commander

all: $(BIN)

$(BUILD):
	mkdir -p $(BUILD)

$(BIN): src/commander.c src/commander_cli.c src/sha256.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ src/commander_cli.c src/commander.c src/sha256.c $(LDFLAGS)

clean:
	rm -f $(BIN)

test-commander: $(BIN)
	@set -e; \
	D=$(BUILD)/law_test; rm -rf $$D; mkdir -p $$D; \
	$(BIN) keygen --law-dir $$D; \
	ENV=$$($(BIN) sign --law-dir $$D --device nb-test --action override_rules); \
	echo "$$ENV" | grep -q grokium; \
	NONCE=$$(echo "$$ENV" | sed -n 's/.*"nonce":"\([^"]*\)".*/\1/p'); \
	TS=$$(echo "$$ENV" | sed -n 's/.*"ts":\([0-9]*\).*/\1/p'); \
	SIG=$$(echo "$$ENV" | sed -n 's/.*"sig":"\([^"]*\)".*/\1/p'); \
	$(BIN) verify --law-dir $$D --device nb-test --action override_rules --nonce $$NONCE --ts $$TS --sig $$SIG; \
	# forged model claim must not verify with garbage sig \
	if $(BIN) verify --law-dir $$D --device nb-test --action override_rules --nonce $$NONCE --ts $$TS --sig $$(printf '00%.0s' {1..128}); then echo FORGE_LEAK; exit 1; fi; \
	echo COMMANDER_LAW_OK
