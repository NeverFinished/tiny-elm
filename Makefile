# TINY 2 - build the portable core, the plain CLI and the shared library
# used by the Textual TUI.

CC      ?= cc
CFLAGS  ?= -O2 -std=c99 -Wall -Wextra -pedantic
UNAME   := $(shell uname -s)
ifeq ($(UNAME),Darwin)
  SOEXT := dylib
  SOFLAGS := -dynamiclib
else
  SOEXT := so
  SOFLAGS := -shared -fPIC
endif

BUILD   := build
LIB     := $(BUILD)/libtiny.$(SOEXT)
CLI     := $(BUILD)/tiny

all: $(LIB) $(CLI)

$(BUILD):
	mkdir -p $(BUILD)

$(LIB): core/tiny.c core/tiny.h | $(BUILD)
	$(CC) $(CFLAGS) -fPIC $(SOFLAGS) -o $@ core/tiny.c -lm

$(CLI): cli/tiny_cli.c core/tiny.c core/tiny.h | $(BUILD)
	$(CC) $(CFLAGS) -Icore -o $@ cli/tiny_cli.c core/tiny.c -lm

# Reference: the original DOS source built in its plain-text mode.
$(BUILD)/tiny_dos: dos/TINY.C | $(BUILD)
	$(CC) -O2 -w -o $@ dos/TINY.C -lm

# Compare answers (creativity 0) of the new core with the original program.
test: $(CLI) $(BUILD)/tiny_dos
	./tests/compare.sh

run: $(LIB)
	python3 -m tui

clean:
	rm -rf $(BUILD)

.PHONY: all test run clean
