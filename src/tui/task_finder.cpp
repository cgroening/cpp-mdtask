#include "tui/task_finder.hpp"

#include "domain/agenda.hpp"
#include "domain/subtasks.hpp"
#include "domain/suggestion.hpp"
#include "tui/finder_actions.hpp"
#include "tui/task_form.hpp"
#include "tui/task_presentation.hpp"
#include "util/date.hpp"

#include <sparcli.hpp>

#include <algorithm>
#include <ctime>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mdtask {

namespace {

// Which view (and therefore which table layout) the finder is rendering.
enum class Layout { TASKS, NOTES, ARCHIVE };

// Per-layout column headers (✎ = note marker, ◌ = status, ☑ = subtasks,
// → = relative due). The TASKS view leads with the status glyph. The *_SEL
// variants prepend an empty-header marker column, shown only while a multi-
// selection is active.
constexpr const char* TASK_HEADERS[]   = {"\xe2\x97\x8c", "!", "\xe2\x98\x91",
                                          "Task", "Due/Done", "\xe2\x86\x92"};
constexpr const char* TASK_HEADERS_SEL[] = {"", "\xe2\x97\x8c", "!",
                                            "\xe2\x98\x91", "Task", "Due/Done",
                                            "\xe2\x86\x92"};
constexpr const char* NOTE_HEADERS[]   = {"Task"};
constexpr const char* NOTE_HEADERS_SEL[] = {"", "Task"};
constexpr const char* ARCHIVE_HEADERS[] = {"\xe2\x9c\x8e", "!", "\xe2\x97\x8c",
                                           "Task", "Due/Done"};

/** Headers, column count and the searched/stretched (title) column for a view. */
struct ColumnSpec {
    const char* const* headers;
    std::size_t n_cols;
    int task_col;
};

ColumnSpec column_spec(Layout layout, bool has_selection) {
    switch(layout) {
        case Layout::NOTES:
            return has_selection ? ColumnSpec{NOTE_HEADERS_SEL, 2, 1}
                                 : ColumnSpec{NOTE_HEADERS, 1, 0};
        case Layout::ARCHIVE: return {ARCHIVE_HEADERS, 5, 3};
        case Layout::TASKS:   break;
    }
    return has_selection ? ColumnSpec{TASK_HEADERS_SEL, 7, 4}
                         : ColumnSpec{TASK_HEADERS, 6, 3};
}

/** Builds an Alt(+Shift)+named-key chord for the reorder shortcuts. */
sparcli::KeyChord alt_key(ScKeyType key, bool shift) {
    return sparcli::KeyChord{
        .key = key,
        .mods = static_cast<std::uint8_t>(
            SC_MOD_ALT | (shift ? SC_MOD_SHIFT : SC_MOD_NONE)
        ),
    };
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

/** Task id for each finder row, or nullopt for a section-header row. */
std::vector<std::optional<std::string>> row_task_ids(const RowIndex& rows) {
    std::vector<std::optional<std::string>> ids;
    ids.reserve(rows.by_index.size());
    for(const auto& entry : rows.by_index) {
        ids.push_back(
            entry ? std::optional<std::string>(entry->id) : std::nullopt
        );
    }
    return ids;
}

/** A multi-selected row is tinted across all of its cells. */
using Selection = std::unordered_set<std::string>;

/** Adds one row in the given `layout` and records it in `rows` at `index`. */
void add_row(
    sparcli::Fuzzy& finder,
    const Task& task,
    std::chrono::year_month_day today,
    DateFormat format,
    Layout layout,
    RowIndex& rows,
    std::size_t& index,
    const Selection& selected
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
        case Layout::TASKS: {
            // Subtask progress "<done>/<total>", blank when the body has none;
            // green once every box is ticked.
            const SubtaskProgress sub = count_subtasks(task.description);
            const std::string sub_text =
                sub.total > 0 ? std::format("{}/{}", sub.done, sub.total) : "";
            const sparcli::TextStyle sub_style =
                (sub.total > 0 && sub.done == sub.total)
                    ? sparcli::style(
                          SC_TEXT_ATTR_NONE, sparcli::palette::green()
                      )
                    : sparcli::style(SC_TEXT_ATTR_DIM);

            // Relative due ("in 3d" / "2d overdue"), only for dated, non-terminal
            // tasks; terminal items show their completion date instead.
            std::string rel_text;
            sparcli::TextStyle rel_style = sparcli::style(SC_TEXT_ATTR_DIM);
            if(task.due && !is_terminal(task.status)) {
                rel_text =
                    presentation::format_relative_due(*task.due, today);
                rel_style =
                    presentation::relative_due_style(*task.due, today);
            }

            fields = {
                presentation::status_symbol(task, overdue),
                presentation::priority_symbol(task.priority),
                sub_text,
                task.title,
                due_done_text,
                rel_text,
            };
            styles = {
                presentation::status_style(task, overdue),
                presentation::priority_style(task.priority),
                sub_style,
                presentation::title_style(task),
                due_done_style,
                rel_style,
            };
            break;
        }
    }
    // While a multi-selection is active, every row gets a leading marker cell
    // (a glyph for the marked rows). The column itself is added in column_spec,
    // so all rows must carry the cell to match the column count.
    if(!selected.empty()) {
        fields.insert(
            fields.begin(),
            presentation::selection_symbol(selected.contains(task.id))
        );
        styles.insert(styles.begin(), presentation::selection_style());
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
    DateFormat format,
    const Selection& selected
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
            add_row(
                finder, task, today, format, Layout::TASKS, rows, index,
                selected
            );
        }
    }
    return rows;
}

/** Fills the finder with a flat, ordered list of notes (no sections). */
RowIndex populate_notes(
    sparcli::Fuzzy& finder,
    const std::vector<Task>& notes,
    std::chrono::year_month_day today,
    DateFormat format,
    const Selection& selected
) {
    RowIndex rows;
    std::size_t index = 0;
    for(const auto& note : notes) {
        add_row(
            finder, note, today, format, Layout::NOTES, rows, index, selected
        );
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
    const Selection none;
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
            add_row(
                finder, task, today, format, Layout::ARCHIVE, rows, index, none
            );
        }
    }
    return rows;
}

/** Shows a message the user dismisses with OK (Enter). */
void acknowledge(std::string_view message) {
    const std::string text(message);
    sparcli::Select dialog(sparcli::SelectOpts{
        .prompt = text.c_str(),
        .accent = sparcli::palette::purple(),
        .box = {
            .enabled = true,
            .border = {.type = SC_BORDER_ROUNDED,
                       .color = sparcli::palette::purple()},
            .width_mode = SC_WIDTH_FULL,
        },
    });
    dialog.add("OK");
    static_cast<void>(dialog.run_one());
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

/** Outcome of the inline due-date picker. */
struct DueChoice {
    bool changed = false;   /**< False when the user cancelled (Esc). */
    std::optional<std::chrono::year_month_day> due;   /**< Empty = cleared. */
};

/** Opens the calendar seeded from `current`; lets the user clear the date. */
DueChoice run_due_picker(std::optional<std::chrono::year_month_day> current) {
    std::tm seed{};
    if(current) {
        seed.tm_year = static_cast<int>(current->year()) - 1900;
        seed.tm_mon =
            static_cast<int>(static_cast<unsigned>(current->month())) - 1;
        seed.tm_mday =
            static_cast<int>(static_cast<unsigned>(current->day()));
    }
    sparcli::DatePickerOpts opts{};
    opts.prompt = "Due date";
    opts.accent = sparcli::palette::purple();
    opts.allow_clear = true;
    opts.box = {
        .enabled = true,
        .border = {.type = SC_BORDER_ROUNDED,
                   .color = sparcli::palette::purple()},
        .width_mode = SC_WIDTH_FULL,
    };

    const auto picked = sparcli::datepicker(seed, opts);
    if(!picked) {
        return {};   // cancelled
    }
    if(sparcli::date_empty(*picked)) {
        return {.changed = true, .due = std::nullopt};
    }
    const std::chrono::year_month_day day{
        std::chrono::year{picked->tm_year + 1900},
        std::chrono::month{static_cast<unsigned>(picked->tm_mon + 1)},
        std::chrono::day{static_cast<unsigned>(picked->tm_mday)},
    };
    return {.changed = true, .due = day};
}

/** Shows the keyboard-shortcut reference as a scrollable, read-only list. */
void run_help() {
    static const char* const HELP_HEADERS[] = {"Key", "Action"};

    sparcli::FuzzyOpts opts{};
    opts.prompt = "Filter shortcuts";
    opts.table = true;
    opts.headers = HELP_HEADERS;
    opts.n_cols = 2;
    opts.search_columns = (std::uint64_t{1} << 0) | (std::uint64_t{1} << 1);
    opts.stretch_columns = std::uint64_t{1} << 1;   // the Action column fills
    opts.order = SC_FUZZY_ORDER_INSERTION;
    opts.section_counts = false;
    opts.modal = true;
    opts.fullscreen = true;
    opts.valign = SC_VALIGN_TOP;
    opts.hide_summary = true;
    opts.empty_text = "";
    opts.box = sparcli::BoxStyle{
        .enabled = true,
        .border = {.type = SC_BORDER_ROUNDED,
                   .color = sparcli::palette::yellow()},
        .padding = {.right = 1, .left = 1},
        .width_mode = SC_WIDTH_FULL,
    };
    opts.table_opts.border.outer_color = sparcli::rgb(180, 180, 180);
    opts.table_opts.border.inner_color = sparcli::rgb(180, 180, 180);

    // The header is borrowed by run(), so it must outlive the finder.
    sparcli::Text title;
    title.append(
        "Keyboard shortcuts",
        sparcli::style(SC_TEXT_ATTR_BOLD, sparcli::palette::yellow())
    );
    title.append("    Esc to close", sparcli::style(SC_TEXT_ATTR_DIM));
    const sparcli::Rendered header = presentation::app_header(title);
    opts.header = header.get();

    sparcli::Fuzzy finder(opts);
    const sparcli::TextStyle section_style =
        sparcli::style(SC_TEXT_ATTR_BOLD, sparcli::palette::yellow());
    const sparcli::TextStyle key_style =
        sparcli::style(SC_TEXT_ATTR_BOLD, sparcli::palette::cyan());
    for(const auto& item : help_entries()) {
        if(!item.section.empty()) {
            finder.add_section_styled(item.section, section_style);
        } else {
            finder.add_row_styled(
                {item.key, item.desc}, {key_style, sparcli::TextStyle{}}
            );
        }
    }
    static_cast<void>(finder.run());   // Esc or Enter closes the help
}

/** Shows the list of skipped (malformed/unreadable) files in a yellow panel. */
void show_load_warnings(const std::vector<std::string>& warnings) {
    constexpr std::size_t MAX_SHOWN = 12;
    std::string text =
        "Some files were skipped and are not shown:\n";
    std::size_t shown = 0;
    for(const auto& warning : warnings) {
        if(shown++ >= MAX_SHOWN) {
            text += std::format("  ... and {} more\n", warnings.size() - shown
                + 1);
            break;
        }
        text += "  - " + warning + "\n";
    }
    text += "\nFix their YAML front matter to bring them back.";

    sparcli::Select dialog(sparcli::SelectOpts{
        .prompt = text.c_str(),
        .accent = sparcli::palette::yellow(),
        .box = {
            .enabled = true,
            .border = {.type = SC_BORDER_ROUNDED,
                       .color = sparcli::palette::yellow()},
            .width_mode = SC_WIDTH_FULL,
        },
    });
    dialog.add("OK");
    static_cast<void>(dialog.run_one());
}

/**
 * Builds the pinned tab bar "Tasks | Notes | Archive" (active tab 0/1/2).
 * When `suggestion` is non-empty it adds a second "Next: ..." banner line.
 */
sparcli::Rendered build_tabbar(
    int active, std::size_t skipped, const std::string& suggestion
) {
    const char* const names[] = {"Tasks", "Notes", "Archive"};
    const sparcli::TextStyle app_title = sparcli::style(
        SC_TEXT_ATTR_BOLD, sparcli::palette::purple()
    );
    const sparcli::TextStyle on =
        sparcli::style(SC_TEXT_ATTR_BOLD, sparcli::palette::green());
    const sparcli::TextStyle off = sparcli::style(SC_TEXT_ATTR_DIM);

    sparcli::Text bar;
    bar.append("mdtask    ", app_title);
    for(int i = 0; i < 3; ++i) {
        if(i > 0) {
            bar.append("  \xe2\x94\x82  ", off);   // " │ "
        }
        bar.append(names[i], i == active ? on : off);
    }
    if(skipped > 0) {
        bar.append(
            std::format("    \xe2\x9a\xa0 {} skipped", skipped),   // ⚠
            sparcli::style(SC_TEXT_ATTR_BOLD, sparcli::palette::yellow())
        );
    }
    if(!suggestion.empty()) {
        bar.append("\n", off);
        bar.append(
            "\xe2\x98\x85 " + suggestion,   // ★
            sparcli::style(SC_TEXT_ATTR_BOLD, sparcli::palette::yellow())
        );
    }
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
    // The header carries its own "(open: x; done: y)" suffix, so the finder's
    // automatic per-section count is switched off.
    opts.section_counts = false;
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

    // Files that could not be parsed are skipped, not fatal; tell the user once
    // up front which ones (the header also keeps a "N skipped" reminder).
    std::vector<std::string> startup_warnings;
    static_cast<void>(service.all_tasks());
    for(auto& warning : service.load_warnings()) {
        startup_warnings.push_back(std::move(warning));
    }
    static_cast<void>(service.notes());
    for(auto& warning : service.load_warnings()) {
        startup_warnings.push_back(std::move(warning));
    }
    if(!startup_warnings.empty()) {
        show_load_warnings(startup_warnings);
    }

    enum class View { TASKS, NOTES };
    View list_view = View::TASKS;  // the active non-archive list
    bool show_archive = false;     // overlays the archive on top of the list
    bool show_suggestion = false;  // banner with the recommended next task
    std::uint64_t focus = 0;       // id to keep the cursor on after a rebuild
    Selection selected;            // task ids marked with Space for bulk actions

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
         .on_return(sparcli::key_special(SC_KEY_BACKSPACE), ACT_DELETE)
         .on_return(sparcli::key_char('?'), ACT_HELP, "help")
         .on_return(sparcli::key_char('q'), ACT_QUIT, "quit");

        if(layout == Layout::ARCHIVE) {
            shortcuts.on_return(sparcli::key_char('r'), ACT_RESTORE, "restore")
                     .on_return(sparcli::key_char('s'), ACT_JUMP, "section");
        } else if(layout == Layout::NOTES) {
            shortcuts.on_return(sparcli::key_char(' '), ACT_TOGGLE_SELECT,
                                "select")
                     .on_return(sparcli::key_char('n'), ACT_NEW, "new")
                     .on_return(sparcli::key_char('N'), ACT_NEW_OTHER,
                                "new task")
                     .on_return(sparcli::key_char('a'), ACT_ARCHIVE, "archive")
                     .on_return(alt_key(SC_KEY_UP, false), ACT_MOVE_UP, "move")
                     .on_return(alt_key(SC_KEY_DOWN, false), ACT_MOVE_DOWN)
                     .on_return(alt_key(SC_KEY_UP, true), ACT_MOVE_TOP, "to end")
                     .on_return(alt_key(SC_KEY_DOWN, true), ACT_MOVE_BOTTOM);
        } else {
            shortcuts.on_return(sparcli::key_char(' '), ACT_TOGGLE_SELECT,
                                "select")
                     .on_return(sparcli::key_char('r'), ACT_TOGGLE_SUGGEST,
                                "next")
                     .on_return(sparcli::key_char('R'), ACT_FOCUS_SUGGEST)
                     .on_return(sparcli::key_char('d'), ACT_TOGGLE_DONE, "done")
                     .on_return(sparcli::key_char('p'), ACT_CYCLE_STATUS,
                                "status")
                     .on_return(sparcli::key_char('t'), ACT_PICK_DATE, "date")
                     .on_return(sparcli::key_char('+'), ACT_SHIFT_PLUS, "+1d")
                     .on_return(sparcli::key_char('='), ACT_SHIFT_PLUS)
                     .on_return(sparcli::key_char('-'), ACT_SHIFT_MINUS, "-1d")
                     .on_return(sparcli::key_char('a'), ACT_ARCHIVE, "archive")
                     .on_return(sparcli::key_char('n'), ACT_NEW, "new")
                     .on_return(sparcli::key_char('N'), ACT_NEW_OTHER,
                                "new note")
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
        // load_warnings() reflects the load that just produced `items`.
        const std::size_t skipped = service.load_warnings().size();

        // The next-task suggestion banner (Tasks view only, while toggled on).
        std::string suggestion_line;
        if(layout == Layout::TASKS && show_suggestion) {
            if(const auto next = suggest_next_task(items)) {
                const std::string when = next->due
                    ? presentation::format_relative_due(*next->due, today)
                    : "no date";
                suggestion_line = std::format(
                    "Next: {}  ({}, {})", next->title, when,
                    presentation::priority_label(next->priority)
                );
            } else {
                suggestion_line = "Next: nothing - all caught up";
            }
        }

        // The header is borrowed by run(), so it must outlive the finder.
        const sparcli::Rendered header =
            build_tabbar(active_tab, skipped, suggestion_line);

        sparcli::FuzzyOpts opts =
            make_opts(column_spec(layout, !selected.empty()));
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
            rows = populate_notes(
                finder, items, today, config.date_format, selected
            );
        } else {
            const auto agenda = build_agenda(items, today);
            rows = populate(finder, agenda, today, config.date_format, selected);
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

        const auto chosen_row = finder.run();
        if(!chosen_row) {
            break;   // Esc / Ctrl-C quits the app
        }
        const int action = shortcuts.fired();

        if(action == ACT_QUIT) {
            break;   // `q` quits the app
        }

        if(action == ACT_HELP) {
            run_help();
            continue;
        }

        if(action == ACT_TOGGLE_LIST) {
            // Switching into an empty list would open a finder with no rows and
            // close immediately, so guard it and stay put with a message.
            const bool to_notes = list_view == View::TASKS;
            const bool target_empty = to_notes ? service.notes().empty()
                                               : service.all_tasks().empty();
            if(target_empty) {
                acknowledge(
                    to_notes
                        ? "No notes yet. Tick 'Note' in the form to make one."
                        : "No tasks yet."
                );
                continue;
            }
            show_archive = false;
            list_view = to_notes ? View::NOTES : View::TASKS;
            focus = 0;
            selected.clear();   // marks do not carry across lists
            continue;
        }

        if(action == ACT_TOGGLE_ARCHIVE) {
            if(!show_archive && service.archived_tasks().empty()) {
                acknowledge("No archived items yet.");
                continue;
            }
            show_archive = !show_archive;
            focus = 0;
            selected.clear();   // the archive is not multi-selectable
            continue;
        }

        if(action == ACT_TOGGLE_SUGGEST) {
            show_suggestion = !show_suggestion;
            // Turning it on jumps the cursor onto the recommended task.
            if(show_suggestion) {
                if(const auto next = suggest_next_task(items)) {
                    focus = row_id(next->id);
                }
            }
            continue;
        }

        if(action == ACT_FOCUS_SUGGEST) {
            // Jump straight to the recommended task without the banner toggle.
            if(const auto next = suggest_next_task(items)) {
                focus = row_id(next->id);
            }
            continue;
        }

        if(action == ACT_JUMP) {
            if(const std::uint64_t target = run_section_jump(jump_targets)) {
                focus = target;
            }
            continue;
        }

        if(action == ACT_NEW || action == ACT_NEW_OTHER) {
            // `n` creates the current view's type; `N` creates the opposite.
            bool default_note = list_view == View::NOTES;
            if(action == ACT_NEW_OTHER) {
                default_note = !default_note;
            }
            if(const auto created =
                   run_new_task_form(service, config, default_note)) {
                // Land on the new item's own list so it is visible.
                focus = row_id(created->id);
                list_view = created->note ? View::NOTES : View::TASKS;
                show_archive = false;
            }
            continue;
        }

        const std::size_t cursor = *chosen_row;
        if(cursor >= rows.by_index.size() || !rows.by_index[cursor]) {
            continue;   // cursor on a header or an empty result
        }
        const Task& task = *rows.by_index[cursor];
        focus = row_id(task.id);

        // Space marks/unmarks the cursor task for the next bulk action.
        if(action == ACT_TOGGLE_SELECT) {
            if(!selected.erase(task.id)) {
                selected.insert(task.id);
            }
            continue;
        }

        // The marked ids in display order, plus the tasks an action targets:
        // the whole selection for bulk-capable actions, else just the cursor.
        std::vector<std::string> selection;
        for(const auto& entry : rows.by_index) {
            if(entry && selected.contains(entry->id)) {
                selection.push_back(entry->id);
            }
        }
        const std::vector<std::string> targets =
            action_targets(action, task.id, selection);
        const bool consumed_selection =
            !selection.empty() && action_is_bulk(action);

        // Delete works in every view, behind a confirm that defaults to No.
        if(action == ACT_DELETE) {
            const std::string prompt = targets.size() > 1
                ? std::format("Delete {} items permanently?", targets.size())
                : "Delete this item permanently?";
            if(sparcli::confirm(prompt).value_or(false)) {
                // Keep the cursor near the gap instead of jumping to the top.
                const auto next = focus_after_delete(row_task_ids(rows), cursor);
                focus = next ? row_id(*next) : 0;
                for(const auto& id : targets) {
                    report(service.delete_task(id));
                }
                selected.clear();
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

        // The calendar picker needs the new date before it can be applied.
        if(action == ACT_PICK_DATE) {
            const DueChoice choice = run_due_picker(task.due);
            if(choice.changed) {
                for(const auto& id : targets) {
                    report(service.set_due(id, choice.due));
                }
                if(consumed_selection) {
                    selected.clear();
                }
            }
            continue;
        }

        // Status cycling needs each target's current status; look it up by id.
        std::unordered_map<std::string, const Task*> by_id;
        for(const auto& item : items) {
            by_id.emplace(item.id, &item);
        }

        switch(action) {
            case ACT_TOGGLE_DONE:
                for(const auto& id : targets) {
                    report(service.toggle_done(id));
                }
                break;
            case ACT_CYCLE_STATUS:
                for(const auto& id : targets) {
                    const auto found = by_id.find(id);
                    if(found != by_id.end()) {
                        report(service.set_status(
                            id, next_status(found->second->status)
                        ));
                    }
                }
                break;
            case ACT_SHIFT_PLUS:
                for(const auto& id : targets) {
                    report(service.shift_due(id, 1));
                }
                break;
            case ACT_SHIFT_MINUS:
                for(const auto& id : targets) {
                    report(service.shift_due(id, -1));
                }
                break;
            case ACT_ARCHIVE:
                for(const auto& id : targets) {
                    report(service.archive_task(id));
                }
                break;
            case ACT_MOVE_UP:
                report(service.move_task(task.id, MoveDir::UP));     break;
            case ACT_MOVE_DOWN:
                report(service.move_task(task.id, MoveDir::DOWN));   break;
            case ACT_MOVE_TOP:
                report(service.move_task(task.id, MoveDir::TOP));    break;
            case ACT_MOVE_BOTTOM:
                report(service.move_task(task.id, MoveDir::BOTTOM)); break;
            default:
                // A bare Enter opens the editor for the cursor item.
                static_cast<void>(run_edit_task_form(service, config, task));
                break;
        }
        if(consumed_selection) {
            selected.clear();
        }
    }
}

}  // namespace mdtask
