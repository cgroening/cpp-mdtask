# CLAUDE.md – mdtask

Project context for Claude Code sessions started in this directory.

## What this is

`mdtask` is a markdown-based task manager in C++26, built on the layered-CLI
template (`../templates/clibase`) and the **sparcli** library
(`../../C/libs/sparcli` – fuzzy finder, forms, datepicker, markdown/YAML serde,
layered config, XDG paths). One Markdown file per task: title = H1, body =
description, metadata in YAML front matter. The main screen is an interactive
fuzzy agenda. Status: **v0.1 complete** (builds clean with `-Werror`, 114 tests
green, ASan/UBSan clean). See `README.md` and `TODO.md`.

## Key design decisions (agreed with the user)

- One `.md` per task; file name `<due-iso>--<slug>.md` or `nodate--<slug>.md`.
  File names always use ISO dates; the stable front-matter `id` is the identity
  source of truth (not the path).
- Title is mirrored in front-matter `title` **and** the body H1, kept in sync.
- Default display date format `DD.MM.YYYY` (config `date_format`, also `iso`).
- Tasks dir defaults to `$XDG_DATA_HOME/mdtask/tasks`; archiving moves files to
  `archive/<year>/<month>/`. `completed_at` recorded on done.
- Task↔project link will live as `project: <id>` in the task front matter
  (v0.2). `src/storage/markdown_document.*` is deliberately task-agnostic – the
  seed for a future reusable `mddb` library.

## Conventions (must follow)

- Style guide (binding): `/Users/cgroening/Library/Mobile Documents/iCloud~md~obsidian/Documents/Corvin/1 Eingang/Claude Code Prompts/Style Guide.md`.
  C++26, 4-space indent, 80-col lines, `if(...)`/`for(...)` no space, braces
  always, Doxygen on functions, English identifiers/comments, layered
  architecture, `std::expected` for expected errors.
- **Never commit. Propose a commit-message title only (imperative English).**
- Build against sparcli via `pkg-config` (installed at `~/.local`). Local
  checkout override: `make SPARCLI_CFLAGS=-I<sparcli>/include SPARCLI_LIBS=<sparcli>/libsparcli.a`.

## Verify after changes

```
make EXTRA_CXXFLAGS=-Werror     # clean build, warnings as errors
make test                       # 114 checks must pass
make sanitize                   # ASan/UBSan clean
# interactive screens (need a real terminal):
MDTASK_TASKS_DIR=$(pwd)/examples/tasks ./bin/mdtask
```

## Next: v0.2

See `TODO.md`: DoneZilla/project management, configurable field schema,
formula/template fields, `mddb` extraction, recurring tasks/tags, Qt GUI.

## Reference

Implementation plan for v0.1: `~/.claude/plans/velvety-munching-noodle.md`.
