# mdtask TODO

## v0.1 (follow-ups / next up)

- **Recurring tasks** (`repeat: daily/weekly/...`). A done occurrence spawns the
  next due date instead of just leaving the active set.
- **Sort within a section by priority** in addition to the manual `order` (e.g. a
  config switch or a secondary sort key in `domain/agenda.cpp::section_less`).
- **`mdtask doctor` command** - an integrity check that reports duplicate `id`s,
  malformed front matter, and (once projects exist) orphaned `project:` links.
- **Task dependencies** (`blocked_by: <id>`): grey out or hide tasks whose
  blockers are still open.
- **Daily review / stats** computed from `completed_at` (e.g. "done today: 7").
- **Open the whole task `.md` file in nvim from the finder.** This **requires
  extending sparcli** with a public, tty-inheriting file editor (e.g.
  `sc_edit_file(cmd, path)` built on the existing internal `run_child`): the
  public `sc_run` captures output and is unsuitable, and `sc_run_editor` is
  internal and temp-file based. mdtask would then re-read and re-save the task
  afterwards to keep the file-name invariant (due/title may have changed).

## v0.2 (next)

- **Project management** and the configurable field schema:
  schema-driven, freely configurable fields (key, label, type, dropdown
  options), so the app works for arbitrary project types - not just tasks.
  Field types: text, number, date, bool, select, multiselect, days_to(date),
  template.
- **Formula fields**: a real expression language. v0.1 will only need string
  interpolation (e.g. a folder name from `{customer}_{offer_no}`); a full
  formula engine comes with DoneZilla. Discuss scope before building.
- **Extract the markdown-database layer into its own `mddb` library.**
  `src/storage/markdown_document.{hpp,cpp}` is deliberately task-agnostic and
  is the extraction seed; the schema/record/query engine joins it there. This
  keeps a future Qt GUI on the same data base.
- **Task <-> project linking**: tasks already carry `project: <id>` in their
  front matter (the single source of truth). Add the project side and,
  optionally, a generated read-only `## TODOs` section in each project file.
- **Tags/contexts** (`@home`, `@work`) for filtering, and a **Qt GUI** on the
  shared data base.
- **Undo (at least one level)** for destructive finder actions (`d`, `a`,
  `Delete`, the status cycle): remember the last state/path and restore it on
  `u`. Cheap with plain Markdown.

## v0.3

- **Quick-add with natural dates** ("tomorrow", "mon", "+3") on `mdtask add` and
  in the form's due field.
- **Full-text search over the body**, not just titles (the finder filter `i`
  currently matches the title column only).
- **iCal / agenda export** (read-only `.ics`) for calendar integration without
  lock-in.

## v0.4

- **First-run wizard / onboarding**: create the tasks dir and explain the basics
  on first launch instead of opening an empty screen.
- **Performance with many files**: measure `list_markdown_files` as the tasks
  dir grows to thousands of `.md`; add caching / lazy loading if needed.

## Known follow-ups

- **Rename keeps project links consistent.** When projects exist, renaming a
  task (changing its due date or title, which renames the file) must update the
  task's link inside its project file. See the `update()` method in
  `src/storage/markdown_task_repository.cpp` - links use the stable front-matter
  `id` today, so this is safe until path-based links are introduced.
- **Optional sparcli helper**: a directory glob ("load all `.md` in a folder").
  Currently handled by `list_markdown_files` in `markdown_document.cpp`; could
  move into sparcli if more apps need it.
