# clibase - layered-architecture CLI template using sparcli.
#
#   make                  build bin/clibase
#   make run              build and run it (pass arguments with ARGS=...)
#   make test             build and run the test suite
#   make sanitize         run the tests under AddressSanitizer/UBSan
#   make compdb           write compile_commands.json for clangd
#   make clean            remove build artefacts
#
# Strict mode (treat warnings as errors): make EXTRA_CXXFLAGS=-Werror

CXX      ?= c++
# -Wno-missing-designated-field-initializers: sparcli's zero-init-friendly
# *Opts structs are meant to be brace-initialized with only the fields you
# set; the rest default to zero. This (non-default) warning is incompatible
# with that idiom (it even fires inside sparcli's own headers), so silence it.
CXXFLAGS ?= -std=c++26 -Wall -Wextra -Wpedantic -Wshadow \
            -Wno-missing-designated-field-initializers
CXXFLAGS += $(EXTRA_CXXFLAGS)
CPPFLAGS += -Isrc

# sparcli is resolved through pkg-config by default. clibase needs a sparcli
# version that includes the framework modules (argument parser, logging, XDG
# paths) - i.e. the umbrella header must include args/sparcli_args.h. To
# build against a local checkout without installing, override both variables:
#   make SPARCLI_CFLAGS=-I/path/to/sparcli/include \
#        SPARCLI_LIBS=/path/to/sparcli/libsparcli.a
SPARCLI_CFLAGS ?= $(shell pkg-config --cflags sparcli 2>/dev/null)
SPARCLI_LIBS   ?= $(shell pkg-config --libs sparcli 2>/dev/null)

ifeq ($(strip $(SPARCLI_LIBS)),)
$(error sparcli not found via pkg-config. Install it (run `make install` in \
the sparcli repo) or pass SPARCLI_CFLAGS / SPARCLI_LIBS - see \
docs/development.md)
endif

# Include sparcli as a system header (-isystem) so that -Wpedantic and other
# strict warnings apply to this project's code only, not to sparcli's C
# headers (which legitimately use C99/C11 features that C++ flags as
# extensions).
CPPFLAGS += $(patsubst -I%,-isystem %,$(SPARCLI_CFLAGS))

BIN   := bin/mdtask
BUILD ?= build

SRC  := $(shell find src -name '*.cpp')
OBJ  := $(SRC:%.cpp=$(BUILD)/%.o)

# Application objects minus the entry point, reused by the test runner.
APP_OBJ := $(filter-out $(BUILD)/src/main.o,$(OBJ))

TEST_SRC := $(wildcard tests/*.cpp)
TEST_OBJ := $(TEST_SRC:%.cpp=$(BUILD)/%.o)
TEST_BIN := $(BUILD)/test_runner

DEPS := $(OBJ:.o=.d) $(TEST_OBJ:.o=.d)

.PHONY: all run test sanitize clean compdb

all: $(BIN)

$(BIN): $(OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(OBJ) $(SPARCLI_LIBS) -o $@

# Pass arguments with ARGS, e.g.  make run ARGS='add "Buy milk"'
run: $(BIN)
	./$(BIN) $(ARGS)

# Test objects also need the test-only headers in tests/.
$(BUILD)/tests/%.o: CPPFLAGS += -Itests

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

test: $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN): $(APP_OBJ) $(TEST_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(APP_OBJ) $(TEST_OBJ) $(SPARCLI_LIBS) -o $@

# Sanitizer objects are incompatible with normal ones, so they get their own
# build directory - a plain `make` afterwards keeps working without a clean.
sanitize:
	$(MAKE) test BUILD=$(BUILD)-sanitize \
	    EXTRA_CXXFLAGS="-fsanitize=address,undefined -g $(EXTRA_CXXFLAGS)"

# Generate compile_commands.json for clangd/editors - no external tools
# needed. It records the real compile flags (including the pkg-config include
# paths, so any sparcli install prefix is resolved without hard-coding it).
# clangd prefers this over compile_flags.txt. Re-run after changing flags or
# moving the sparcli install. compile_commands.json is machine-specific and
# git-ignored.
compdb:
	@{ \
	  printf '[\n'; \
	  first=1; \
	  for f in $(SRC); do \
	    [ $$first = 1 ] && first=0 || printf ',\n'; \
	    printf '  {"directory": "%s", "file": "%s", "command": "%s %s %s -c %s"}' \
	      "$(CURDIR)" "$$f" "$(CXX)" "$(CXXFLAGS)" "$(CPPFLAGS)" "$$f"; \
	  done; \
	  for f in $(TEST_SRC); do \
	    printf ',\n'; \
	    printf '  {"directory": "%s", "file": "%s", "command": "%s %s %s -Itests -c %s"}' \
	      "$(CURDIR)" "$$f" "$(CXX)" "$(CXXFLAGS)" "$(CPPFLAGS)" "$$f"; \
	  done; \
	  printf '\n]\n'; \
	} > compile_commands.json
	@echo "wrote compile_commands.json"

clean:
	rm -rf $(BUILD) $(BUILD)-sanitize bin

-include $(DEPS)
