#include "tui/task_finder.hpp"

#include "domain/agenda.hpp"
#include "domain/recurrence.hpp"
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
// PLACEHOLDER is used for an empty view (or the not-yet-implemented Recurring
// view): a single disabled info row, so the finder still has content to show.
enum class Layout { TASKS, NOTES, ARCHIVE, PLACEHOLDER };

// Per-layout column headers (✎ = note marker, ◌ = status, ☑ = subtasks,
// → = relative due). The TASKS view leads with the status glyph. The *_SEL
// variants prepend an empty-header marker column, shown only while a multi-
// selection is active.
constexpr const char* TASK_HEADERS[]   = {"\xe2\x97\x8c", "!", "\xe2\x98\x91",
                                          "@", "Task", "Due/Done",
                                          "\xe2\x86\x92"};
constexpr const char* TASK_HEADERS_SEL[] = {"", "\xe2\x97\x8c", "!",
                                            "\xe2\x98\x91", "@", "Task",
                                            "Due/Done", "\xe2\x86\x92"};
constexpr const char* NOTE_HEADERS[]   = {"Task"};
constexpr const char* NOTE_HEADERS_SEL[] = {"", "Task"};
constexpr const char* ARCHIVE_HEADERS[] = {"\xe2\x9c\x8e", "!", "\xe2\x97\x8c",
                                           "Task", "Due/Done"};
constexpr const char* PLACEHOLDER_HEADERS[] = {""};

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
        case Layout::ARCHIVE:     return {ARCHIVE_HEADERS, 5, 3};
        case Layout::PLACEHOLDER: return {PLACEHOLDER_HEADERS, 1, 0};
        case Layout::TASKS:       break;
    }
    return has_selection ? ColumnSpec{TASK_HEADERS_SEL, 8, 5}
                         : ColumnSpec{TASK_HEADERS, 7, 4};
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

/** The configured category for a task's label, or nullptr for none/unknown. */
const CategoryDef* find_category(
    const std::vector<CategoryDef>& categories, const std::string& name
) {
    if(name.empty()) {
        return nullptr;
    }
    for(const auto& category : categories) {
        if(category.name == name) {
            return &category;
        }
    }
    return nullptr;
}

/** Adds one row in the given `layout` and records it in `rows` at `index`. */
void add_row(
    sparcli::Fuzzy& finder,
    const Task& task,
    std::chrono::year_month_day today,
    DateFormat format,
    Layout layout,
    RowIndex& rows,
    std::size_t& index,
    const Selection& selected,
    const std::vector<CategoryDef>& categories
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
        case Layout::PLACEHOLDER:   // single title column (never reached here)
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

            // A leading ↻ marks a recurring task in the agenda.
            const std::string title_text = task.recurrence
                ? "\xe2\x86\xbb " + task.title   // ↻
                : task.title;

            // Category badge: the configured short form in its own colors,
            // blank for "no category".
            const CategoryDef* category =
                find_category(categories, task.category);
            const std::string cat_text =
                category ? category->shortform : "";
            const sparcli::TextStyle cat_style = category
                ? sparcli::style(SC_TEXT_ATTR_NONE, category->fg, category->bg)
                : sparcli::style(SC_TEXT_ATTR_NONE);

            fields = {
                presentation::status_symbol(task, overdue),
                presentation::priority_symbol(task.priority),
                sub_text,
                cat_text,
                title_text,
                due_done_text,
                rel_text,
            };
            styles = {
                presentation::status_style(task, overdue),
                presentation::priority_style(task.priority),
                sub_style,
                cat_style,
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
    // Keep the first row for an id, so focusing a recurring task (whose id
    // repeats across projected occurrences) lands on its earliest event.
    rows.index_by_id.emplace(id, index);
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
    Language language,
    const Selection& selected,
    const std::vector<CategoryDef>& categories
) {
    RowIndex rows;
    std::size_t index = 0;

    for(const auto& section : agenda) {
        finder.add_section_styled(
            presentation::section_header(section, today, format, language),
            presentation::section_style(section, today)
        );
        rows.by_index.emplace_back(std::nullopt);
        ++index;

        for(const auto& task : section.tasks) {
            add_row(
                finder, task, today, format, Layout::TASKS, rows, index,
                selected, categories
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
            finder, note, today, format, Layout::NOTES, rows, index, selected,
            {}
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
                finder, task, today, format, Layout::ARCHIVE, rows, index, none,
                {}
            );
        }
    }
    return rows;
}

/** Fills the finder with one disabled info row (empty / not-yet views). */
RowIndex populate_placeholder(sparcli::Fuzzy& finder, const std::string& text) {
    RowIndex rows;
    finder.add_row_styled({text}, {sparcli::style(SC_TEXT_ATTR_DIM)});
    finder.set_disabled(0);          // non-selectable, greyed
    rows.by_index.emplace_back(std::nullopt);   // no task behind this row
    return rows;
}

/**
 * Projects the recurring tasks into virtual dated occurrences for the Recurring
 * view: each recurring task yields one copy per upcoming occurrence (this and
 * next week, at least its next date). The copies keep the real id, so id-based
 * actions still resolve to the underlying task; only the due date is virtual.
 */
std::vector<Task> recurring_occurrences(
    const std::vector<Task>& tasks, std::chrono::year_month_day today
) {
    const auto window_end = end_of_next_week(today);
    std::vector<Task> occurrences;
    for(const auto& task : tasks) {
        if(!task.recurrence) {
            continue;
        }
        const auto from_due = task.due.value_or(today);
        for(const auto& day :
            upcoming_occurrences(*task.recurrence, from_due, today, window_end)) {
            Task occurrence = task;
            occurrence.due = day;
            occurrences.push_back(std::move(occurrence));
        }
    }
    return occurrences;
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
    const char* const names[] = {
        "[1] Tasks", "[2] Recurring", "[3] Notes", "[4] Archive", "[5] Search"
    };
    const sparcli::TextStyle app_title = sparcli::style(
        SC_TEXT_ATTR_BOLD, sparcli::palette::purple()
    );
    const sparcli::TextStyle on =
        sparcli::style(SC_TEXT_ATTR_BOLD, sparcli::palette::green());
    const sparcli::TextStyle off = sparcli::style(SC_TEXT_ATTR_DIM);

    sparcli::Text bar;
    bar.append("mdtask    ", app_title);
    for(int i = 0; i < 5; ++i) {
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

    enum class View { TASKS, RECURRING, NOTES, ARCHIVE, SEARCH };
    View view = View::TASKS;        // the active view (chosen with keys 1-5)
    bool show_suggestion = false;   // banner with the recommended next task
    std::uint64_t focus = 0;        // id to keep the cursor on after a rebuild
    Selection selected;             // task ids marked with Space for bulk actions

    // One alternate-screen session spans the whole loop, so switching between
    // the views and the edit form never flickers.
    sparcli::AltScreen screen;

    for(;;) {
        const auto today = mdtask::today();

        // Gather the items for the current view.
        std::vector<Task> items;
        switch(view) {
            case View::TASKS:   items = service.all_tasks(); break;
            case View::NOTES:   items = service.notes(); sort_notes(items); break;
            case View::ARCHIVE: items = service.archived_tasks(); break;
            case View::RECURRING:
                items = recurring_occurrences(service.all_tasks(), today);
                break;
            case View::SEARCH:  break;   // placeholder, no items
        }
        // An empty view (or a not-yet-built view) renders a single disabled
        // placeholder row, so the finder still has content to show.
        const bool placeholder = (view == View::SEARCH) || items.empty();
        const Layout layout = placeholder ? Layout::PLACEHOLDER
            : view == View::NOTES   ? Layout::NOTES
            : view == View::ARCHIVE ? Layout::ARCHIVE
                                    : Layout::TASKS;
        const int active_tab = static_cast<int>(view);

        // Shortcuts are rebuilt each iteration so labels match the view; they
        // must outlive run(). They are the single source for both the footer
        // hints and the `?` help screen (sparcli::show_shortcuts), so each
        // carries a footer text and/or a longer help text plus a section. The
        // declaration order here is the order shown in both places.
        using D = sparcli::ShortcutDisplay;
        sparcli::Shortcuts shortcuts;

        // Navigation: the built-in finder keys (help only) and the view
        // switches. The tab bar already shows the views, so 1-5 stay out of the
        // footer (in_footer = false) but remain documented in the help screen.
        const auto add_navigation = [&] {
            shortcuts.section("Navigation")
                .help_row("\xe2\x86\x91/\xe2\x86\x93 or j/k", "move cursor")
                .help_row("i", "filter (type to search); Esc back to normal")
                .help_row("Enter", "edit / open the item")
                .on_return(sparcli::key_char('1'), ACT_VIEW_TASKS,
                           D{.help = "switch to Tasks", .in_footer = false})
                .on_return(sparcli::key_char('2'), ACT_VIEW_RECURRING,
                           D{.help = "switch to Recurring", .in_footer = false})
                .on_return(sparcli::key_char('3'), ACT_VIEW_NOTES,
                           D{.help = "switch to Notes", .in_footer = false})
                .on_return(sparcli::key_char('4'), ACT_VIEW_ARCHIVE,
                           D{.help = "switch to Archive", .in_footer = false})
                .on_return(sparcli::key_char('5'), ACT_VIEW_SEARCH,
                           D{.help = "switch to Search", .in_footer = false});
        };

        // General: present in every view. q and ? show in the footer; the two
        // delete bindings are documented in the help screen only.
        const auto add_general = [&] {
            shortcuts.section("General")
                .on_return(sparcli::key_char('q'), ACT_QUIT, D{.footer = "quit"})
                .on_return(sparcli::key_char('?'), ACT_HELP,
                           D{.footer = "help", .help = "show this help"})
                .on_return(sparcli::key_special(SC_KEY_DELETE), ACT_DELETE,
                           D{.footer = "delete", .help = "delete permanently"})
                .on_return(sparcli::key_special(SC_KEY_BACKSPACE), ACT_DELETE,
                           D{.help = "delete permanently (same as Del)",
                             .in_footer = false});
        };

        add_navigation();

        if(view == View::ARCHIVE) {
            shortcuts.section("Archive")
                .on_return(sparcli::key_char('r'), ACT_RESTORE,
                           D{.footer = "restore", .help = "restore the item"})
                .on_return(sparcli::key_char('s'), ACT_JUMP,
                           D{.footer = "section", .help = "jump to a section"});
        } else if(view == View::RECURRING) {
            // Rows are projected occurrences; Enter edits the underlying series
            // in the form (see the default action), `e` opens its raw .md file,
            // and `s` jumps between the day sections.
            shortcuts.section("Actions")
                .on_return(sparcli::key_char('e'), ACT_EDIT_FILE,
                           D{.footer = "edit file",
                             .help = "open the series .md file in $EDITOR"})
                .on_return(sparcli::key_char('n'), ACT_NEW,
                           D{.footer = "new", .help = "new task"})
                .on_return(sparcli::key_char('s'), ACT_JUMP,
                           D{.footer = "section", .help = "jump to a section"});
        } else if(view == View::NOTES) {
            shortcuts.section("Actions (Notes)")
                .on_return(sparcli::key_char('e'), ACT_EDIT_FILE,
                           D{.footer = "edit",
                             .help = "open the whole .md file in $EDITOR"})
                .on_return(sparcli::key_char('c'), ACT_DUPLICATE,
                           D{.footer = "copy",
                             .help = "duplicate (adds a numbered (copy) suffix)"})
                .on_return(sparcli::key_char('n'), ACT_NEW,
                           D{.footer = "new", .help = "new note"})
                .on_return(sparcli::key_char('N'), ACT_NEW_OTHER,
                           D{.footer = "new task", .help = "new task instead"})
                .on_return(sparcli::key_char('a'), ACT_ARCHIVE,
                           D{.footer = "archive", .help = "archive"})
                .on_return(alt_key(SC_KEY_UP, false), ACT_MOVE_UP,
                           D{.footer = "move", .help = "reorder up"})
                .on_return(alt_key(SC_KEY_DOWN, false), ACT_MOVE_DOWN,
                           D{.help = "reorder down", .in_footer = false})
                .on_return(alt_key(SC_KEY_UP, true), ACT_MOVE_TOP,
                           D{.footer = "to end", .help = "move to top"})
                .on_return(alt_key(SC_KEY_DOWN, true), ACT_MOVE_BOTTOM,
                           D{.help = "move to bottom", .in_footer = false});
            shortcuts.section("Multi-select")
                .help_row("Space then d / a / Del", "apply to every marked item")
                .on_return(sparcli::key_char(' '), ACT_TOGGLE_SELECT,
                           D{.footer = "select",
                             .help = "mark / unmark the item"});
        } else if(view == View::TASKS) {
            shortcuts.section("Actions (Tasks)")
                .on_return(sparcli::key_char('e'), ACT_EDIT_FILE,
                           D{.footer = "edit",
                             .help = "open the whole .md file in $EDITOR"})
                .on_return(sparcli::key_char('c'), ACT_DUPLICATE,
                           D{.footer = "copy",
                             .help = "duplicate (adds a numbered (copy) suffix)"})
                .on_return(sparcli::key_char('w'), ACT_TOGGLE_SUGGEST,
                           D{.footer = "next",
                             .help = "toggle the next-task suggestion"})
                .on_return(sparcli::key_char('W'), ACT_FOCUS_SUGGEST,
                           D{.help = "jump to the suggested next task",
                             .in_footer = false})
                .on_return(sparcli::key_char('d'), ACT_TOGGLE_DONE,
                           D{.footer = "done", .help = "toggle done"})
                .on_return(sparcli::key_char('p'), ACT_CYCLE_STATUS,
                           D{.footer = "status",
                             .help = "cycle status (open / in progress / "
                                     "paused / cancelled)"})
                .on_return(sparcli::key_char('t'), ACT_PICK_DATE,
                           D{.footer = "date",
                             .help = "pick a due date (calendar)"})
                .on_return(sparcli::key_char('+'), ACT_SHIFT_PLUS,
                           D{.footer = "+1d",
                             .help = "shift the due date by one day"})
                .on_return(sparcli::key_char('='), ACT_SHIFT_PLUS,
                           D{.help = "shift the due date by one day (same as +)",
                             .in_footer = false})
                .on_return(sparcli::key_char('-'), ACT_SHIFT_MINUS,
                           D{.footer = "-1d",
                             .help = "shift the due date back one day"})
                .on_return(sparcli::key_char('a'), ACT_ARCHIVE,
                           D{.footer = "archive", .help = "archive"})
                .on_return(sparcli::key_char('n'), ACT_NEW,
                           D{.footer = "new", .help = "new task"})
                .on_return(sparcli::key_char('N'), ACT_NEW_OTHER,
                           D{.footer = "new note", .help = "new note instead"})
                .on_return(sparcli::key_char('s'), ACT_JUMP,
                           D{.footer = "section", .help = "jump to a section"})
                .on_return(alt_key(SC_KEY_UP, false), ACT_MOVE_UP,
                           D{.footer = "move", .help = "reorder up"})
                .on_return(alt_key(SC_KEY_DOWN, false), ACT_MOVE_DOWN,
                           D{.help = "reorder down", .in_footer = false})
                .on_return(alt_key(SC_KEY_UP, true), ACT_MOVE_TOP,
                           D{.footer = "to end", .help = "move to top"})
                .on_return(alt_key(SC_KEY_DOWN, true), ACT_MOVE_BOTTOM,
                           D{.help = "move to bottom", .in_footer = false});
            shortcuts.section("Multi-select")
                .help_row("Space then d / p / a / t / + / - / Del",
                          "apply to every marked item")
                .on_return(sparcli::key_char(' '), ACT_TOGGLE_SELECT,
                           D{.footer = "select",
                             .help = "mark / unmark the item"});
        }

        add_general();

        // load_warnings() reflects the load that just produced `items`.
        const std::size_t skipped = service.load_warnings().size();

        // The next-task suggestion banner (Tasks view only, while toggled on).
        std::string suggestion_line;
        if(view == View::TASKS && !placeholder && show_suggestion) {
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
        shortcuts.apply(opts);
        sparcli::Fuzzy finder(opts);

        RowIndex rows;
        std::vector<JumpTarget> jump_targets;
        if(placeholder) {
            const char* message =
                view == View::RECURRING
                    ? "No recurring tasks - add a 'repeat:' field to a task"
                : view == View::SEARCH
                    ? "The full search feature is coming soon."
                : view == View::NOTES   ? "No notes yet - press n to add one"
                : view == View::ARCHIVE ? "No archived items yet"
                                        : "No tasks yet - press n to add one";
            rows = populate_placeholder(finder, message);
        } else if(layout == Layout::ARCHIVE) {
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
            rows = populate(
                finder, agenda, today, config.date_format, config.language,
                selected, config.categories
            );
            for(const auto& section : agenda) {
                jump_targets.push_back({
                    presentation::section_header(
                        section, today, config.date_format, config.language
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
            sparcli::show_shortcuts(
                shortcuts,
                sparcli::ShortcutHelpOpts{
                    .title = "mdtask - keyboard shortcuts",
                    // The agenda finder holds an AltScreen for the whole loop,
                    // so the help screen must reuse it (and render fullscreen)
                    // rather than open a nested one.
                    .in_alt_screen = true,
                }
            );
            continue;
        }

        if(action == ACT_VIEW_TASKS || action == ACT_VIEW_RECURRING
           || action == ACT_VIEW_NOTES || action == ACT_VIEW_ARCHIVE
           || action == ACT_VIEW_SEARCH) {
            // Keys 1-5 switch the view directly; an empty target just shows its
            // placeholder row instead of being blocked.
            switch(action) {
                case ACT_VIEW_TASKS:     view = View::TASKS;     break;
                case ACT_VIEW_RECURRING: view = View::RECURRING; break;
                case ACT_VIEW_NOTES:     view = View::NOTES;     break;
                case ACT_VIEW_ARCHIVE:   view = View::ARCHIVE;   break;
                case ACT_VIEW_SEARCH:    view = View::SEARCH;    break;
                default:                                         break;
            }
            focus = 0;
            selected.clear();   // marks do not carry across views
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
            bool default_note = view == View::NOTES;
            if(action == ACT_NEW_OTHER) {
                default_note = !default_note;
            }
            if(const auto created =
                   run_new_task_form(service, config, default_note)) {
                // Land on the new item's own view so it is visible: notes in
                // Notes, recurring tasks in Recurring (on their first event),
                // everything else in Tasks.
                focus = row_id(created->id);
                view = created->note          ? View::NOTES
                     : created->recurrence    ? View::RECURRING
                                              : View::TASKS;
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

        // Open the whole .md file in $EDITOR. edit_file must not run inside an
        // alt-screen session, so we leave and re-enter it around the call; then
        // reload + re-save so a changed due/title renames the file.
        if(action == ACT_EDIT_FILE) {
            const auto path = service.file_path(task.id);
            if(!path) {
                sparcli::alert::warning("Could not locate the task's file.");
                continue;
            }
            sc_altscreen_end();
            const int rc = config.editor.empty()
                ? sparcli::edit_file(path->string())
                : sparcli::edit_file(config.editor, path->string());
            sc_altscreen_begin();
            if(rc != 0) {
                sparcli::alert::warning(
                    "Could not open the editor (no terminal or editor not "
                    "found)."
                );
                continue;
            }
            if(const auto reloaded = service.reload_task(task.id)) {
                focus = row_id(reloaded->id);
            } else {
                sparcli::alert::warning(
                    "Edited file could not be reloaded (invalid front matter?) "
                    "- left unchanged."
                );
            }
            continue;
        }

        // Duplicate the cursor item and land the cursor on the new copy.
        if(action == ACT_DUPLICATE) {
            const auto copy = service.duplicate_task(task.id);
            if(copy) {
                focus = row_id(copy->id);
            } else {
                report(copy);
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
            }
            continue;
        }

        // The archive view is read-only apart from restoring an item.
        if(view == View::ARCHIVE) {
            if(action == ACT_RESTORE) {
                report(service.restore_task(task.id));
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
                // A bare Enter opens the editor for the cursor item. In the
                // Recurring view the row is a virtual occurrence (its due date
                // is projected), so edit the real stored series instead of the
                // projection - otherwise saving would persist the wrong date.
                if(view == View::RECURRING) {
                    const auto all = service.all_tasks();
                    const auto real = std::ranges::find(all, task.id, &Task::id);
                    if(real != all.end()) {
                        static_cast<void>(
                            run_edit_task_form(service, config, *real)
                        );
                    }
                } else {
                    static_cast<void>(run_edit_task_form(service, config, task));
                }
                break;
        }
        if(consumed_selection) {
            selected.clear();
        }
    }
}

}  // namespace mdtask
