#include "tui/task_finder.hpp"

#include "domain/agenda.hpp"
#include "tui/task_form.hpp"
#include "tui/task_presentation.hpp"
#include "util/date.hpp"

#include <sparcli.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace mdtask {

namespace {

// Which view (and therefore which table layout) the finder is rendering.
enum class Layout { TASKS, NOTES, ARCHIVE };

// Per-layout column headers (✎ = note marker, ◌ = status).
constexpr const char* TASK_HEADERS[]   = {"!", "\xe2\x97\x8c", "Task",
                                          "Due/Done"};
constexpr const char* NOTE_HEADERS[]   = {"Task"};
constexpr const char* ARCHIVE_HEADERS[] = {"\xe2\x9c\x8e", "!", "\xe2\x97\x8c",
                                           "Task", "Due/Done"};

/** Headers, column count and the searched/stretched (title) column for a view. */
struct ColumnSpec {
    const char* const* headers;
    std::size_t n_cols;
    int task_col;
};

ColumnSpec column_spec(Layout layout) {
    switch(layout) {
        case Layout::NOTES:   return {NOTE_HEADERS, 1, 0};
        case Layout::ARCHIVE: return {ARCHIVE_HEADERS, 5, 3};
        case Layout::TASKS:   break;
    }
    return {TASK_HEADERS, 4, 2};
}

// Shortcut ids reported via Shortcuts::fired() (-1 = a bare Enter).
enum {
    ACT_TOGGLE_DONE    = 1,
    ACT_SHIFT_PLUS     = 2,
    ACT_SHIFT_MINUS    = 3,
    ACT_ARCHIVE        = 4,
    ACT_NEW            = 5,
    ACT_TOGGLE_ARCHIVE = 6,
    ACT_RESTORE        = 7,
    ACT_JUMP           = 8,
    ACT_CYCLE_STATUS   = 9,
    ACT_MOVE_UP        = 10,
    ACT_MOVE_DOWN      = 11,
    ACT_MOVE_TOP       = 12,
    ACT_MOVE_BOTTOM    = 13,
    ACT_TOGGLE_LIST    = 14,
    ACT_DELETE         = 15,
};

/** Builds an Alt(+Shift)+named-key chord for the reorder shortcuts. */
sparcli::KeyChord alt_key(ScKeyType key, bool shift) {
    return sparcli::KeyChord{
        .key = key,
        .mods = static_cast<std::uint8_t>(
            SC_MOD_ALT | (shift ? SC_MOD_SHIFT : SC_MOD_NONE)
        ),
    };
}

/** Status cycle (key `p`): open -> in progress -> paused -> cancelled -> open.
    Done is set with `d`, not via this cycle; cycling a done task reopens it. */
Status next_status(Status status) {
    switch(status) {
        case Status::OPEN:        return Status::IN_PROGRESS;
        case Status::IN_PROGRESS: return Status::PAUSED;
        case Status::PAUSED:      return Status::CANCELLED;
        case Status::CANCELLED:   break;
        case Status::DONE:        break;
    }
    return Status::OPEN;
}

/** A section the jump picker can move the cursor to. */
struct JumpTarget {
    std::string label;        /**< Section header shown in the picker. */
    std::uint64_t first_id;   /**< Finder id of the section's first task. */
};

/** Stable finder row id derived from a task's id. */
std::uint64_t row_id(const std::string& task_id) {
    return std::hash<std::string>{}(task_id);
}

/** Maps add-order indices to their task, with gaps for section headers. */
struct RowIndex {
    std::vector<std::optional<Task>> by_index;
    std::unordered_map<std::uint64_t, std::size_t> index_by_id;
};

/**
 * Picks which task to focus after deleting the row at `cursor`, so the cursor
 * stays put instead of jumping to the top: the row that slides up into the gap
 * (next in the same section), else the row above in the section, else the first
 * row of the next section, else the nearest row of the previous section.
 *
 * @return The focus id of the chosen task, or 0 when nothing is left.
 */
std::uint64_t focus_after_delete(const RowIndex& rows, std::size_t cursor) {
    const auto& by_index = rows.by_index;
    const std::size_t count = by_index.size();

    // Tasks of one section are contiguous; a neighbour task means same section.
    if(cursor + 1 < count && by_index[cursor + 1]) {
        return row_id(by_index[cursor + 1]->id);
    }
    if(cursor > 0 && by_index[cursor - 1]) {
        return row_id(by_index[cursor - 1]->id);
    }
    // The section is now empty: first task of the next section...
    for(std::size_t i = cursor + 1; i < count; ++i) {
        if(by_index[i]) {
            return row_id(by_index[i]->id);
        }
    }
    // ...otherwise the nearest task in a previous section.
    for(std::size_t i = cursor; i-- > 0;) {
        if(by_index[i]) {
            return row_id(by_index[i]->id);
        }
    }
    return 0;
}

/** Adds one row in the given `layout` and records it in `rows` at `index`. */
void add_row(
    sparcli::Fuzzy& finder,
    const Task& task,
    std::chrono::year_month_day today,
    DateFormat format,
    Layout layout,
    RowIndex& rows,
    std::size_t& index
) {
    const bool overdue =
        task.due && *task.due < today && !is_terminal(task.status);

    // The Due/Done column shows the completion date (green) for a terminal
    // item, otherwise the due date (dim).
    const std::string completed = presentation::format_completed(
        task.completed_at, format
    );
    std::string due_done_text;
    sparcli::TextStyle due_done_style;
    if(is_terminal(task.status) && !completed.empty()) {
        due_done_text = completed;
        due_done_style = sparcli::style(
            SC_TEXT_ATTR_NONE, sparcli::palette::green()
        );
    } else {
        due_done_text =
            task.due ? presentation::format_date(*task.due, format) : "";
        due_done_style = sparcli::style(SC_TEXT_ATTR_DIM);
    }

    std::vector<std::string> fields;
    std::vector<sparcli::TextStyle> styles;
    switch(layout) {
        case Layout::NOTES:
            fields = {task.title};
            styles = {presentation::title_style(task)};
            break;
        case Layout::ARCHIVE:
            fields = {
                presentation::note_symbol(task),
                presentation::priority_symbol(task.priority),
                presentation::status_symbol(task, overdue),
                task.title,
                due_done_text,
            };
            styles = {
                presentation::note_style(),
                presentation::priority_style(task.priority),
                presentation::status_style(task, overdue),
                presentation::title_style(task),
                due_done_style,
            };
            break;
        case Layout::TASKS:
            fields = {
                presentation::priority_symbol(task.priority),
                presentation::status_symbol(task, overdue),
                task.title,
                due_done_text,
            };
            styles = {
                presentation::priority_style(task.priority),
                presentation::status_style(task, overdue),
                presentation::title_style(task),
                due_done_style,
            };
            break;
    }
    finder.add_row_styled(fields, styles);

    const std::uint64_t id = row_id(task.id);
    finder.set_id(index, id);
    rows.index_by_id[id] = index;
    rows.by_index.push_back(task);
    ++index;
}

/** Orders notes by manual order (unset last), then title. */
void sort_notes(std::vector<Task>& notes) {
    std::ranges::sort(notes, [](const Task& a, const Task& b) {
        if(a.order != b.order) {
            if(!a.order) { return false; }
            if(!b.order) { return true; }
            return *a.order < *b.order;
        }
        return a.title < b.title;
    });
}

/** Fills the finder with the agenda's sections and rows; returns the index. */
RowIndex populate(
    sparcli::Fuzzy& finder,
    const std::vector<AgendaSection>& agenda,
    std::chrono::year_month_day today,
    DateFormat format
) {
    RowIndex rows;
    std::size_t index = 0;

    for(const auto& section : agenda) {
        finder.add_section_styled(
            presentation::section_header(section, today, format),
            presentation::section_style(section, today)
        );
        rows.by_index.emplace_back(std::nullopt);
        ++index;

        for(const auto& task : section.tasks) {
            add_row(finder, task, today, format, Layout::TASKS, rows, index);
        }
    }
    return rows;
}

/** Fills the finder with a flat, ordered list of notes (no sections). */
RowIndex populate_notes(
    sparcli::Fuzzy& finder,
    const std::vector<Task>& notes,
    std::chrono::year_month_day today,
    DateFormat format
) {
    RowIndex rows;
    std::size_t index = 0;
    for(const auto& note : notes) {
        add_row(finder, note, today, format, Layout::NOTES, rows, index);
    }
    return rows;
}

/** Fills the finder with archived items grouped by completion month/year. */
RowIndex populate_archive(
    sparcli::Fuzzy& finder,
    const std::vector<ArchiveGroup>& groups,
    std::chrono::year_month_day today,
    DateFormat format
) {
    RowIndex rows;
    std::size_t index = 0;
    const sparcli::TextStyle header_style = sparcli::style(
        SC_TEXT_ATTR_BOLD,
        sparcli::palette::fg_darken_2(),
        sparcli::palette::bg_lighten_3()
    );

    for(const auto& group : groups) {
        finder.add_section_styled(group.header, header_style);
        rows.by_index.emplace_back(std::nullopt);
        ++index;

        for(const auto& task : group.tasks) {
            add_row(finder, task, today, format, Layout::ARCHIVE, rows, index);
        }
    }
    return rows;
}

/** Opens a section picker; returns the chosen target's first-task id, or 0. */
std::uint64_t run_section_jump(const std::vector<JumpTarget>& targets) {
    if(targets.size() < 2) {
        return 0;   // nothing to pick between
    }
    sparcli::Select picker(sparcli::SelectOpts{
        .prompt = "Jump to section",
        .accent = sparcli::palette::purple(),
        .box = {
            .enabled = true,
            .border = {.type = SC_BORDER_ROUNDED,
                       .color = sparcli::palette::purple()},
            .width_mode = SC_WIDTH_FULL,
        },
    });
    for(const auto& target : targets) {
        picker.add(target.label);
    }
    if(const auto choice = picker.run_one()) {
        return targets[*choice].first_id;
    }
    return 0;
}

/** Builds the pinned tab bar "Tasks | Notes | Archive" (active tab 0/1/2). */
sparcli::Rendered build_tabbar(int active, std::size_t count) {
    const char* const names[] = {"Tasks", "Notes", "Archive"};
    const sparcli::TextStyle on =
        sparcli::style(SC_TEXT_ATTR_BOLD, sparcli::palette::purple());
    const sparcli::TextStyle off = sparcli::style(SC_TEXT_ATTR_DIM);

    sparcli::Text bar;
    bar.append("mdtask  ", on);
    for(int i = 0; i < 3; ++i) {
        if(i > 0) {
            bar.append("  \xe2\x94\x82  ", off);   // " │ "
        }
        bar.append(names[i], i == active ? on : off);
    }
    bar.append(
        "   " + std::to_string(count),
        sparcli::style(SC_TEXT_ATTR_NONE, sparcli::palette::cyan())
    );
    return presentation::app_header(bar);
}

/** Builds the finder options (styles and behaviour) for one run. */
sparcli::FuzzyOpts make_opts(const ColumnSpec& cols) {
    sparcli::FuzzyOpts opts{};
    opts.prompt = "Find";
    opts.table = true;
    opts.headers = cols.headers;
    opts.n_cols = cols.n_cols;
    opts.search_columns = std::uint64_t{1} << cols.task_col;
    opts.stretch_columns = std::uint64_t{1} << cols.task_col;
    opts.order = SC_FUZZY_ORDER_INSERTION;
    opts.section_counts = true;
    opts.modal = true;                 // vim-style normal/insert modes
    opts.fullscreen = true;            // fill the alternate screen
    opts.valign = SC_VALIGN_TOP;
    opts.hide_summary = true;
    // Accent and the selected-row highlight come from the global input theme
    // (purple / dark-purple bar); only the section header keeps a custom style.
    opts.empty_text = "No tasks - press n to add one";
    // Section headers are styled per category in populate() via
    // presentation::section_style, so no global section_style is set here.
    opts.box = sparcli::BoxStyle{
        .enabled = true,
        .border = {.type = SC_BORDER_ROUNDED, .color = sparcli::palette::purple()},
        .padding = {.right = 1, .left = 1},
        .width_mode = SC_WIDTH_FULL,
    };
    // The table's own grid lines (frame + separators) in a light gray.
    opts.table_opts.border.outer_color = sparcli::rgb(180, 180, 180);
    opts.table_opts.border.inner_color = sparcli::rgb(180, 180, 180);
    return opts;
}

/** Logs a failed service operation without interrupting the loop. */
void report(const Result<Task>& result) {
    if(!result) {
        sparcli::alert::warning(result.error().message);
    }
}

}  // namespace

void run_task_finder(TaskService& service, const Config& config) {
    if(!sparcli::input_available()) {
        sparcli::alert::warning(
            "Run mdtask in a real terminal (not under a pipe)."
        );
        return;
    }

    // Nothing to show at all: warn and bail out (the finder would otherwise
    // open empty). Notes count too, since they have their own view.
    if(service.all_tasks().empty() && service.notes().empty()) {
        sparcli::alert::warning(
            "No tasks yet. Create one with 'mdtask add <title>'."
        );
        return;
    }

    enum class View { TASKS, NOTES };
    View list_view = View::TASKS;  // the active non-archive list
    bool show_archive = false;     // overlays the archive on top of the list
    std::uint64_t focus = 0;       // id to keep the cursor on after a rebuild

    // One alternate-screen session spans the whole loop, so switching between
    // the views and the edit form never flickers.
    sparcli::AltScreen screen;

    for(;;) {
        const auto today = mdtask::today();
        const Layout layout = show_archive ? Layout::ARCHIVE
            : (list_view == View::NOTES ? Layout::NOTES : Layout::TASKS);
        const int active_tab =
            show_archive ? 2 : (list_view == View::NOTES ? 1 : 0);

        // Shortcuts are rebuilt each iteration so labels match the view; they
        // must outlive run().
        sparcli::Shortcuts shortcuts;
        shortcuts.on_return(
            sparcli::key_char('v'), ACT_TOGGLE_LIST,
            list_view == View::TASKS ? "notes" : "tasks"
        ).on_return(
            sparcli::key_char('b'), ACT_TOGGLE_ARCHIVE,
            show_archive ? "back" : "archive"
        ).on_return(sparcli::key_special(SC_KEY_DELETE), ACT_DELETE, "delete")
         .on_return(sparcli::key_special(SC_KEY_BACKSPACE), ACT_DELETE);

        if(layout == Layout::ARCHIVE) {
            shortcuts.on_return(sparcli::key_char('r'), ACT_RESTORE, "restore")
                     .on_return(sparcli::key_char('s'), ACT_JUMP, "section");
        } else if(layout == Layout::NOTES) {
            shortcuts.on_return(sparcli::key_char('n'), ACT_NEW, "new")
                     .on_return(sparcli::key_char('a'), ACT_ARCHIVE, "archive")
                     .on_return(alt_key(SC_KEY_UP, false), ACT_MOVE_UP, "move")
                     .on_return(alt_key(SC_KEY_DOWN, false), ACT_MOVE_DOWN)
                     .on_return(alt_key(SC_KEY_UP, true), ACT_MOVE_TOP, "to end")
                     .on_return(alt_key(SC_KEY_DOWN, true), ACT_MOVE_BOTTOM);
        } else {
            shortcuts.on_return(sparcli::key_char('d'), ACT_TOGGLE_DONE, "done")
                     .on_return(sparcli::key_char('p'), ACT_CYCLE_STATUS,
                                "status")
                     .on_return(sparcli::key_char('+'), ACT_SHIFT_PLUS, "+1d")
                     .on_return(sparcli::key_char('='), ACT_SHIFT_PLUS)
                     .on_return(sparcli::key_char('-'), ACT_SHIFT_MINUS, "-1d")
                     .on_return(sparcli::key_char('a'), ACT_ARCHIVE, "archive")
                     .on_return(sparcli::key_char('n'), ACT_NEW, "new")
                     .on_return(sparcli::key_char('s'), ACT_JUMP, "section")
                     .on_return(alt_key(SC_KEY_UP, false), ACT_MOVE_UP, "move")
                     .on_return(alt_key(SC_KEY_DOWN, false), ACT_MOVE_DOWN)
                     .on_return(alt_key(SC_KEY_UP, true), ACT_MOVE_TOP, "to end")
                     .on_return(alt_key(SC_KEY_DOWN, true), ACT_MOVE_BOTTOM);
        }

        // Gather the items for the current view.
        std::vector<Task> items;
        if(layout == Layout::ARCHIVE) {
            items = service.archived_tasks();
        } else if(layout == Layout::NOTES) {
            items = service.notes();
            sort_notes(items);
        } else {
            items = service.all_tasks();
        }

        // The header is borrowed by run(), so it must outlive the finder.
        const sparcli::Rendered header = build_tabbar(active_tab, items.size());

        sparcli::FuzzyOpts opts = make_opts(column_spec(layout));
        opts.header = header.get();
        opts.empty_text = layout == Layout::ARCHIVE ? "No archived items"
            : layout == Layout::NOTES ? "No notes - press n to add one"
                                      : "No tasks - press n to add one";
        shortcuts.apply(opts);
        sparcli::Fuzzy finder(opts);

        RowIndex rows;
        std::vector<JumpTarget> jump_targets;
        if(layout == Layout::ARCHIVE) {
            const auto groups = group_archive(items, today);
            rows = populate_archive(finder, groups, today, config.date_format);
            for(const auto& group : groups) {
                jump_targets.push_back(
                    {group.header, row_id(group.tasks.front().id)}
                );
            }
        } else if(layout == Layout::NOTES) {
            rows = populate_notes(finder, items, today, config.date_format);
        } else {
            const auto agenda = build_agenda(items, today);
            rows = populate(finder, agenda, today, config.date_format);
            for(const auto& section : agenda) {
                jump_targets.push_back({
                    presentation::section_header(
                        section, today, config.date_format
                    ),
                    row_id(section.tasks.front().id),
                });
            }
        }
        if(focus != 0) {
            if(const auto found = rows.index_by_id.find(focus);
               found != rows.index_by_id.end()) {
                finder.set_cursor(found->second);
            }
        }

        const auto selected = finder.run();
        if(!selected) {
            break;   // Esc / Ctrl-C quits the app
        }
        const int action = shortcuts.fired();

        if(action == ACT_TOGGLE_LIST) {
            show_archive = false;
            list_view = list_view == View::TASKS ? View::NOTES : View::TASKS;
            focus = 0;
            continue;
        }

        if(action == ACT_TOGGLE_ARCHIVE) {
            if(!show_archive && service.archived_tasks().empty()) {
                sparcli::alert::warning("No archived items yet.");
                continue;
            }
            show_archive = !show_archive;
            focus = 0;
            continue;
        }

        if(action == ACT_JUMP) {
            if(const std::uint64_t target = run_section_jump(jump_targets)) {
                focus = target;
            }
            continue;
        }

        if(action == ACT_NEW) {
            const bool as_note = list_view == View::NOTES && !show_archive;
            if(const auto created =
                   run_new_task_form(service, config, as_note)) {
                focus = row_id(created->id);
            }
            continue;
        }

        const std::size_t cursor = *selected;
        if(cursor >= rows.by_index.size() || !rows.by_index[cursor]) {
            continue;   // cursor on a header or an empty result
        }
        const Task& task = *rows.by_index[cursor];
        focus = row_id(task.id);

        // Delete works in every view, behind a confirm that defaults to No.
        if(action == ACT_DELETE) {
            if(sparcli::confirm("Delete this item permanently?")
                   .value_or(false)) {
                // Keep the cursor near the gap instead of jumping to the top.
                focus = focus_after_delete(rows, cursor);
                report(service.delete_task(task.id));
                if(show_archive && service.archived_tasks().empty()) {
                    show_archive = false;
                }
            }
            continue;
        }

        // The archive view is read-only apart from restoring an item.
        if(show_archive) {
            if(action == ACT_RESTORE) {
                report(service.restore_task(task.id));
                if(service.archived_tasks().empty()) {
                    show_archive = false;
                }
            }
            continue;
        }

        switch(action) {
            case ACT_TOGGLE_DONE: report(service.toggle_done(task.id)); break;
            case ACT_CYCLE_STATUS:
                report(service.set_status(task.id, next_status(task.status)));
                break;
            case ACT_MOVE_UP:
                report(service.move_task(task.id, MoveDir::UP));     break;
            case ACT_MOVE_DOWN:
                report(service.move_task(task.id, MoveDir::DOWN));   break;
            case ACT_MOVE_TOP:
                report(service.move_task(task.id, MoveDir::TOP));    break;
            case ACT_MOVE_BOTTOM:
                report(service.move_task(task.id, MoveDir::BOTTOM)); break;
            case ACT_SHIFT_PLUS:  report(service.shift_due(task.id, 1)); break;
            case ACT_SHIFT_MINUS: report(service.shift_due(task.id, -1)); break;
            case ACT_ARCHIVE:     report(service.archive_task(task.id)); break;
            default:
                // A bare Enter opens the editor for the cursor item.
                static_cast<void>(run_edit_task_form(service, config, task));
                break;
        }
    }
}

}  // namespace mdtask
