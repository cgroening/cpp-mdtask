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

// Table columns. The title is column 1 so the priority marker can lead.
enum { COL_PRIORITY, COL_TASK, COL_DUE, COL_STATUS, N_COLS };

// Shortcut ids reported via Shortcuts::fired() (-1 = a bare Enter).
enum {
    ACT_TOGGLE_DONE = 1,
    ACT_SHIFT_PLUS  = 2,
    ACT_SHIFT_MINUS = 3,
    ACT_ARCHIVE     = 4,
    ACT_NEW         = 5,
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

/** Fills the finder with the agenda's sections and rows; returns the index. */
RowIndex populate(
    sparcli::Fuzzy& finder,
    const std::vector<AgendaSection>& agenda,
    std::chrono::year_month_day today,
    DateFormat format
) {
    RowIndex rows;
    std::size_t index = 0;
    const sparcli::TextStyle due_style = sparcli::style(SC_TEXT_ATTR_DIM);

    for(const auto& section : agenda) {
        finder.add_section(presentation::section_header(section, today, format));
        rows.by_index.emplace_back(std::nullopt);
        ++index;

        for(const auto& task : section.tasks) {
            const bool overdue =
                task.due && *task.due < today && !task.done;
            const std::string due_text =
                task.due ? presentation::format_date(*task.due, format) : "";

            finder.add_row_styled(
                {
                    presentation::priority_symbol(task.priority),
                    task.title,
                    due_text,
                    presentation::status_text(task, overdue),
                },
                {
                    presentation::priority_style(task.priority),
                    presentation::title_style(task),
                    due_style,
                    presentation::status_style(task, overdue),
                }
            );

            const std::uint64_t id = row_id(task.id);
            finder.set_id(index, id);
            rows.index_by_id[id] = index;
            rows.by_index.push_back(task);
            ++index;
        }
    }
    return rows;
}

/** Builds the finder options (styles and behaviour) for one run. */
sparcli::FuzzyOpts make_opts(const char* const* headers) {
    sparcli::FuzzyOpts opts{};
    opts.prompt = "Tasks";
    opts.table = true;
    opts.headers = headers;
    opts.n_cols = N_COLS;
    opts.search_columns = std::uint64_t{1} << COL_TASK;
    opts.order = SC_FUZZY_ORDER_INSERTION;
    opts.section_counts = true;
    opts.modal = true;
    opts.accent = sparcli::palette::accent();
    opts.empty_text = "No tasks - press n to add one";
    opts.selected_style =
        sparcli::style(SC_TEXT_ATTR_NONE, sparcli::black());
    opts.section_style =
        sparcli::style(SC_TEXT_ATTR_BOLD, sparcli::white(), sparcli::rgb(58, 64, 92));
    opts.box = sparcli::BoxStyle{
        .enabled = true,
        .border = {.type = SC_BORDER_ROUNDED, .color = sparcli::palette::accent()},
        .padding = {.right = 1, .left = 1},
    };
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

    static const char* const HEADERS[N_COLS] = {"!", "Task", "Due", "Status"};

    // The shortcut set is borrowed by every run, so it lives for the loop.
    sparcli::Shortcuts shortcuts;
    shortcuts.on_return(sparcli::key_char('d'), ACT_TOGGLE_DONE, "done")
             .on_return(sparcli::key_char('+'), ACT_SHIFT_PLUS, "+1d")
             .on_return(sparcli::key_char('='), ACT_SHIFT_PLUS)
             .on_return(sparcli::key_char('-'), ACT_SHIFT_MINUS, "-1d")
             .on_return(sparcli::key_char('a'), ACT_ARCHIVE, "archive")
             .on_return(sparcli::key_char('n'), ACT_NEW, "new");

    std::uint64_t focus = 0;   // task id to keep the cursor on after a rebuild

    for(;;) {
        const auto tasks = service.all_tasks();
        const auto today = mdtask::today();
        const auto agenda = build_agenda(tasks, today);

        sparcli::FuzzyOpts opts = make_opts(HEADERS);
        shortcuts.apply(opts);
        sparcli::Fuzzy finder(opts);

        const RowIndex rows = populate(finder, agenda, today, config.date_format);
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

        switch(action) {
            case ACT_TOGGLE_DONE: report(service.toggle_done(task.id)); break;
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
