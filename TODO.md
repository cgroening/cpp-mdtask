# mdtask TODO

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
- **Recurring tasks** (`repeat: daily/weekly/...`), **tags/contexts**
  (`@home`, `@work`) for filtering, and a **Qt GUI** on the shared data base.

## Known follow-ups

- **Rename keeps project links consistent.** When projects exist, renaming a
  task (changing its due date or title, which renames the file) must update the
  task's link inside its project file. See the `update()` method in
  `src/storage/markdown_task_repository.cpp` - links use the stable front-matter
  `id` today, so this is safe until path-based links are introduced.
- **Open the whole task file in nvim from the finder.** v0.1 edits the
  description through the form's multiline field (sparcli manages the terminal
  for the editor). Opening the entire `.md` file directly in nvim from the
  finder needs a tty-inheriting child process (sparcli's `sc_run` captures
  output and is unsuitable); decide on the mechanism in v0.2.
- **Optional sparcli helper**: a directory glob ("load all `.md` in a folder").
  Currently handled by `list_markdown_files` in `markdown_document.cpp`; could
  move into sparcli if more apps need it.
- **Quick-add with natural dates** ("tomorrow", "mon", "+3"), a daily review
  using `completed_at`, optional time-of-day on tasks, and snooze.
