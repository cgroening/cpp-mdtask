#include "tui/task_finder.hpp"

#include "domain/agenda.hpp"
#include "tui/task_form.hpp"
#include "tui/task_presentation.hpp"
#include "util/date.hpp"

#include <sparcli.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace mdtask {

namespace {

// Table columns: two leading icon columns (priority, status), then the title
// and the due/done date.
enum { COL_PRIORITY, COL_STATUS, COL_TASK, COL_DUE, N_COLS };

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

/** Next status when cycling: open -> in progress -> done -> open. */
Status next_status(Status status) {
    switch(status) {
        case Status::OPEN:        return Status::IN_PROGRESS;
        case Status::IN_PROGRESS: return Status::DONE;
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

/** Adds one task row to the finder and records it in `rows` at `index`. */
void add_task_row(
    sparcli::Fuzzy& finder,
    const Task& task,
    std::chrono::year_month_day today,
    DateFormat format,
    RowIndex& rows,
    std::size_t& index
) {
    const bool overdue =
        task.due && *task.due < today && task.status != Status::DONE;

    // The Due/Done column shows the completion date (green) for a done task,
    // otherwise the due date (dim).
    const std::string completed = presentation::format_completed(
        task.completed_at, format
    );
    std::string due_done_text;
    sparcli::TextStyle due_done_style;
    if(task.status == Status::DONE && !completed.empty()) {
        due_done_text = completed;
        due_done_style = sparcli::style(
            SC_TEXT_ATTR_NONE, sparcli::palette::green()
        );
    } else {
        due_done_text =
            task.due ? presentation::format_date(*task.due, format) : "";
        due_done_style = sparcli::style(SC_TEXT_ATTR_DIM);
    }

    finder.add_row_styled(
        {
            presentation::priority_symbol(task.priority),
            presentation::status_symbol(task, overdue),
            task.title,
            due_done_text,
        },
        {
            presentation::priority_style(task.priority),
            presentation::status_style(task, overdue),
            presentation::title_style(task),
            due_done_style,
        }
    );

    const std::uint64_t id = row_id(task.id);
    finder.set_id(index, id);
    rows.index_by_id[id] = index;
    rows.by_index.push_back(task);
    ++index;
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
            add_task_row(finder, task, today, format, rows, index);
        }
    }
    return rows;
}

/** Fills the finder with archived tasks grouped by completion month/year. */
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
            add_task_row(finder, task, today, format, rows, index);
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

/** Builds the pinned full-screen header for the agenda or the archive view. */
sparcli::Rendered build_header(std::size_t task_count, bool archive) {
    sparcli::Text title;
    title.append(
        "mdtask",
        sparcli::style(SC_TEXT_ATTR_BOLD, sparcli::palette::purple())
    );
    if(archive) {
        title.append(
            "  archive",
            sparcli::style(SC_TEXT_ATTR_BOLD, sparcli::palette::yellow())
        );
    }
    title.append("  ", sparcli::style(SC_TEXT_ATTR_DIM));
    const std::string noun =
        archive ? " archived" : (task_count == 1 ? " task" : " tasks");
    title.append(
        std::to_string(task_count) + noun,
        sparcli::style(SC_TEXT_ATTR_NONE, sparcli::palette::cyan())
    );
    return presentation::app_header(title);
}

/** Builds the finder options (styles and behaviour) for one run. */
sparcli::FuzzyOpts make_opts(const char* const* headers) {
    sparcli::FuzzyOpts opts{};
    opts.prompt = "Tasks";
    opts.table = true;
    opts.headers = headers;
    opts.n_cols = N_COLS;
    opts.search_columns = std::uint64_t{1} << COL_TASK;
    opts.stretch_columns = std::uint64_t{1} << COL_TASK;
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

    // Without any tasks the agenda is empty and the finder would close at
    // once, leaving the user with a blank screen. Warn and bail out instead.
    if(service.all_tasks().empty()) {
        sparcli::alert::warning(
            "No tasks yet. Create one with 'mdtask add <title>'."
        );
        return;
    }

    static const char* const HEADERS[N_COLS] =
        {"!", "\xe2\x97\x8c", "Task", "Due/Done"};   // ◌ = status

    std::uint64_t focus = 0;    // task id to keep the cursor on after a rebuild
    bool show_archive = false;  // toggles the agenda vs the read-only archive

    // One alternate-screen session spans the whole loop, so switching between
    // the agenda, the archive view and the edit form never flickers.
    sparcli::AltScreen screen;

    for(;;) {
        const auto today = mdtask::today();

        // The shortcut set is borrowed by run(), so it must outlive the finder;
        // it is rebuilt each iteration so its labels match the current view.
        sparcli::Shortcuts shortcuts;
        if(show_archive) {
            shortcuts.on_return(
                sparcli::key_char('v'), ACT_TOGGLE_ARCHIVE, "agenda"
            ).on_return(sparcli::key_char('r'), ACT_RESTORE, "restore")
             .on_return(sparcli::key_char('s'), ACT_JUMP, "section");
        } else {
            shortcuts.on_return(sparcli::key_char('d'), ACT_TOGGLE_DONE, "done")
                     .on_return(sparcli::key_char('p'), ACT_CYCLE_STATUS,
                                "progress")
                     .on_return(sparcli::key_char('+'), ACT_SHIFT_PLUS, "+1d")
                     .on_return(sparcli::key_char('='), ACT_SHIFT_PLUS)
                     .on_return(sparcli::key_char('-'), ACT_SHIFT_MINUS, "-1d")
                     .on_return(sparcli::key_char('a'), ACT_ARCHIVE, "archive")
                     .on_return(sparcli::key_char('n'), ACT_NEW, "new")
                     .on_return(sparcli::key_char('s'), ACT_JUMP, "section")
                     .on_return(sparcli::key_char('v'), ACT_TOGGLE_ARCHIVE,
                                "archive view")
                     .on_return(alt_key(SC_KEY_UP, false), ACT_MOVE_UP, "move")
                     .on_return(alt_key(SC_KEY_DOWN, false), ACT_MOVE_DOWN)
                     .on_return(alt_key(SC_KEY_UP, true), ACT_MOVE_TOP, "to end")
                     .on_return(alt_key(SC_KEY_DOWN, true), ACT_MOVE_BOTTOM);
        }

        const auto active = service.all_tasks();
        std::vector<Task> archived;
        if(show_archive) {
            archived = service.archived_tasks();
        }
        const std::size_t count =
            show_archive ? archived.size() : active.size();

        // The header is borrowed by run(), so it must outlive the finder.
        const sparcli::Rendered header = build_header(count, show_archive);

        sparcli::FuzzyOpts opts = make_opts(HEADERS);
        opts.header = header.get();
        if(show_archive) {
            opts.empty_text = "No archived tasks";
        }
        shortcuts.apply(opts);
        sparcli::Fuzzy finder(opts);

        // Populate the view and, in step, collect the section jump targets.
        RowIndex rows;
        std::vector<JumpTarget> jump_targets;
        if(show_archive) {
            const auto groups = group_archive(archived, today);
            rows = populate_archive(finder, groups, today, config.date_format);
            for(const auto& group : groups) {
                jump_targets.push_back(
                    {group.header, row_id(group.tasks.front().id)}
                );
            }
        } else {
            const auto agenda = build_agenda(active, today);
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

        if(action == ACT_TOGGLE_ARCHIVE) {
            if(!show_archive && service.archived_tasks().empty()) {
                sparcli::alert::warning("No archived tasks yet.");
                continue;
            }
            show_archive = !show_archive;
            focus = 0;   // start at the top of the view we switch to
            continue;
        }

        if(action == ACT_JUMP) {
            if(const std::uint64_t target = run_section_jump(jump_targets)) {
                focus = target;   // land on the section's first task on rebuild
            }
            continue;
        }

        if(action == ACT_NEW) {
            if(const auto created = run_new_task_form(service, config)) {
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

        // In the archive view the only mutating action is restoring a task
        // back into the agenda; everything else leaves it untouched.
        if(show_archive) {
            if(action == ACT_RESTORE) {
                report(service.restore_task(task.id));
                if(service.archived_tasks().empty()) {
                    show_archive = false;   // nothing left to show
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
                // A bare Enter opens the editor for the cursor task.
                static_cast<void>(run_edit_task_form(service, config, task));
                break;
        }
    }
}

}  // namespace mdtask
