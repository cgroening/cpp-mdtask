# mdtask TODO

## v0.1 – Initial Release

- [x] **Next task suggestion** based on due date and priority for an even faster workflow
- [x] **Open the whole task `.md` file in nvim from the finder.**
- [x] **Duplication of tasks and notes** for quicker creation of similar tasks
- [x] **Recurring tasks** (`repeat: daily/weekly/...`). A done occurrence rolls the same task forward to its next due date (every x days/weeks/months or on specific week days) instead of leaving the active set; the schedule basis is per task (`repeat_from: due` or `completion`). The Recurring view shows upcoming occurrences for the current and next week (at least the next one).
- [ ] **Task categories** to group and filter tasks by a free-form label (e.g. `@home`, `work`, `errands`), stored in the front matter and usable by the v0.2 filter and search. (In v1.0, once project management exists, a task can additionally be assigned to a specific project from there.)

## v0.2 – Search, filter and review

- [ ] **Filtering tasks** to only show tasks with specific priority, status, category etc.
- [ ] **Sort within a section by priority** in addition or as an alternative to the manual `order` (e.g. a config switch or a secondary sort key in `domain/agenda.cpp::section_less`).
- [ ] **Full-text search over the body**, not just titles (the finder filter `i` currently matches the title column only) in a separate view.
- [ ] **Daily review / stats** computed from `completed_at` (e.g. "done today: 7").

## v0.3 – Workflow and onboarding

- [ ] **Optimized CLI subcommands** for quick actions without the necessity to open the fuzzy finder
- [ ] **Quick-add with natural dates** ("tomorrow", "mon", "+3") on `mdtask add` and in the form's due field.
- [ ] **Task dependencies** (`blocked_by: <id>`): grey out tasks whose blockers are still open.

## v0.4 – Performance and stability

- [ ] **Performance with many files**: measure `list_markdown_files` as the tasks dir grows to thousands of `.md`; add caching / lazy loading if needed.
- [ ] **`mdtask doctor` command** – an integrity check that reports duplicate `id`s, malformed front matter, and (once projects exist) orphaned `project:` links.
- [ ] **Undo (at least one level)** for destructive finder actions (`d`, `a`, `Delete`, the status cycle): remember the last state/path and restore it on `u`. Cheap with plain Markdown.

## v0.5 – Export & Backup

- [ ] **iCal export** (`.ics`) for calendar integration without lock-in.
- [ ] **Backup Feature** to combine all files in a ZIP archive and copy them to a configured folder

## v0.8 – Polish

- [ ] **First-run wizard / onboarding**: create the tasks dir and explain the basics on first launch instead of opening an empty screen.

## v0.9 – Groundwork for 1.0

- [ ] **Rename keeps project links consistent.** When projects exist, renaming a task (changing its due date or title, which renames the file) must update the task's link inside its project file. See the `update()` method in `src/storage/markdown_task_repository.cpp` - links use the stable front-matter `id` today, so this is safe until path-based links are introduced.
- [ ] **Optional sparcli helper**: a directory glob ("load all `.md` in a folder"). Currently handled by `list_markdown_files` in `markdown_document.cpp`; could move into sparcli if more apps need it.
- [ ] **Extract the markdown-database layer into its own `mddb` library.** `src/storage/markdown_document.{hpp,cpp}` is deliberately task-agnostic and is the extraction seed; the schema/record/query engine joins it there. This keeps a future Qt GUI on the same data base.

## v1.0 – Project management

- [ ] **Project management** and the configurable field schema: schema-driven, freely configurable fields (key, label, type, dropdown options), so the app works for arbitrary project types - not just tasks. Field types: text, number, date, bool, select, multiselect, days_to(date), template.
- [ ] **Formula fields**: a real expression language. v0.1 will only need string interpolation (e.g. a folder name from `{customer}_{offer_no}`); a full formula engine comes with DoneZilla. Discuss scope before building.
- [ ] **Task <-> project linking**: tasks already carry `project: <id>` in their front matter (the single source of truth). Add the project side and, optionally, a generated read-only `## TODOs` section in each project file.

## After 1.0 – Full GUI with Qt

- [ ] **Full GUI with Qt** – The CLI/TUI stays fully intact and is maintained in parallel, so both interfaces can be used interchangeably on the same files.
