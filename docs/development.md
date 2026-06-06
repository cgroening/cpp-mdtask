# Development

How to build, run, and test mdtask.

## Prerequisites

- **A C++26 compiler.** Verify: `c++ -std=c++26 -x c++ -E - </dev/null`
  prints nothing and exits 0. (Apple clang 17+, GCC 14+.)
- **`make` and `pkg-config`.**
- **sparcli with the framework modules *and* the C++ wrapper.** This is the
  important one – see below.

### Installing sparcli (framework + C++ wrapper)

mdtask uses sparcli's application framework: the argument parser
(`args/sparcli_args.h`), logging (`log/sparcli_log.h`), XDG paths and pretty
errors (`app/sparcli_app.h`) plus the C++ wrapper (`sparcli.hpp`). An older
install that only has the output/input widgets will **not** build – the
compile fails with `'args/sparcli_args.h' file not found` or with unknown
`sparcli::Args` / `sparcli::logging` symbols.

```sh
cd /path/to/sparcli           # the sparcli checkout
make
make install                  # or: sudo make install / PREFIX=$HOME/.local
```

Confirm the install is complete:

```sh
pkg-config --exists sparcli && echo found
incdir="$(pkg-config --variable=includedir sparcli)"
ls "$incdir"/sparcli.hpp                # C++ wrapper - must exist
ls "$incdir"/args/sparcli_args.h        # framework - must exist
```

If either file is missing, the installed copy is outdated – re-install from
a sparcli checkout that has the framework modules.

### Building without installing

To build against a local sparcli checkout instead of an installed copy,
override both variables (the Makefile errors out with this hint if
pkg-config finds nothing):

```sh
make SPARCLI_CFLAGS=-I/path/to/sparcli/include \
     SPARCLI_LIBS=/path/to/sparcli/libsparcli.a
```

## Build and run

```sh
make            # compiles to bin/mdtask

# Run the binary directly (simplest):
./bin/mdtask                       # interactive fuzzy agenda (needs a tty)
./bin/mdtask add "Buy milk"
./bin/mdtask list
./bin/mdtask done <id>
./bin/mdtask config
./bin/mdtask --version

# Or via make. `make run` alone opens the agenda; pass arguments with ARGS:
make run                       # -> fuzzy agenda
make run ARGS='add "Buy milk"'
make run ARGS=list

# Try it with the bundled sample tasks:
MDTASK_TASKS_DIR=$(pwd)/examples/tasks ./bin/mdtask
```

Note: `make run add` does not work – `make` reads `add` as a target, not an
argument. Use `ARGS=` or call the binary directly.

Tasks are stored as Markdown files in `$XDG_DATA_HOME/mdtask/tasks/` (falling
back to `~/.local/share/mdtask/tasks/`); the debug log goes to
`$XDG_STATE_HOME/mdtask/mdtask.log`. Run `mdtask config` to see the resolved
paths.

## Test

```sh
make test       # builds build/test_runner and runs it
make sanitize   # same tests under AddressSanitizer + UBSan
```

The suite is dependency-free (see `tests/check.hpp`). It covers:

- **`tests/test_agenda.cpp`** – the pure section grouping/ordering.
- **`tests/test_task_service.cpp`** – business rules, run against the
  `InMemoryTaskRepository` fake (no filesystem).
- **`tests/test_markdown_document.cpp`** – front-matter/body round-trip.
- **`tests/test_markdown_task_repository.cpp`** – slug/file naming, the
  rename-on-change behaviour and archiving, in a throwaway temp directory.
- **`tests/test_config_loader.cpp`** – layered config: defaults, TOML file
  overrides, environment overrides, invalid values, malformed files.

Add a test by writing `run_*_tests()` in a new `tests/*.cpp`, declaring it
in `tests/test_suite.hpp`, and calling it from `tests/test_main.cpp`.

## Editor setup (clangd)

clangd does not run `pkg-config`, so it cannot guess where sparcli is
installed. Generate a compilation database from the real build flags (no
extra tools needed):

```sh
make compdb        # writes compile_commands.json with the actual include paths
```

clangd prefers `compile_commands.json` over the static `compile_flags.txt`
fallback and picks it up automatically (restart the LSP / reopen the file).
The file is machine-specific and git-ignored – each developer runs
`make compdb` once (and again after the sparcli install location or the
compile flags change).

When building against a local checkout, pass the same overrides:

```sh
make compdb SPARCLI_CFLAGS=-I/path/to/sparcli/include \
            SPARCLI_LIBS=/path/to/sparcli/libsparcli.a
```

## After a change

```sh
make EXTRA_CXXFLAGS=-Werror   # build clean, warnings as errors
make test                     # all checks pass
make sanitize                 # ASan/UBSan clean
```

## Pre-commit checklist

- [ ] `make EXTRA_CXXFLAGS=-Werror` builds with no warnings.
- [ ] `make test` is green.
- [ ] `make sanitize` is clean.
- [ ] New behaviour lives in the service (testable) and is covered by a test.
- [ ] Dependencies still point only downward (no layer includes one above it).
- [ ] New commands declare their arguments in `configure()` (no hand-rolled
      argv parsing).
