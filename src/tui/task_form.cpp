#include "tui/task_form.hpp"

#include "domain/recurrence.hpp"
#include "tui/task_presentation.hpp"

#include <sparcli.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <optional>
#include <string>
#include <vector>

namespace mdtask {

namespace {

// The "●" marker signals a set priority (the level shows in the table color).
const std::vector<std::string> PRIORITY_CHOICES = {
    "none", "\xe2\x97\x8f low", "\xe2\x97\x8f medium", "\xe2\x97\x8f high",
};

/** Maps a select index to a priority (out-of-range falls back to NONE). */
Priority priority_from_index(std::size_t index) {
    switch(index) {
        case 1:  return Priority::LOW;
        case 2:  return Priority::MEDIUM;
        case 3:  return Priority::HIGH;
        default: return Priority::NONE;
    }
}

/** Maps a priority to its select index. */
std::size_t priority_to_index(Priority priority) {
    switch(priority) {
        case Priority::LOW:    return 1;
        case Priority::MEDIUM: return 2;
        case Priority::HIGH:   return 3;
        case Priority::NONE:   break;
    }
    return 0;
}

const std::vector<std::string> STATUS_CHOICES = {
    presentation::status_choice(Status::OPEN),
    presentation::status_choice(Status::IN_PROGRESS),
    presentation::status_choice(Status::PAUSED),
    presentation::status_choice(Status::DONE),
    presentation::status_choice(Status::CANCELLED),
};

/** Maps a select index to a status (out-of-range falls back to OPEN). */
Status status_from_index(std::size_t index) {
    switch(index) {
        case 1:  return Status::IN_PROGRESS;
        case 2:  return Status::PAUSED;
        case 3:  return Status::DONE;
        case 4:  return Status::CANCELLED;
        default: return Status::OPEN;
    }
}

/** Maps a status to its select index. */
std::size_t status_to_index(Status status) {
    switch(status) {
        case Status::IN_PROGRESS: return 1;
        case Status::PAUSED:      return 2;
        case Status::DONE:        return 3;
        case Status::CANCELLED:   return 4;
        case Status::OPEN:        break;
    }
    return 0;
}

// The recurrence wizard's choices: the "Repeat" mode (index 0 = none, 1..4 =
// interval units, 5 = a weekday set), the schedule basis, and the weekday
// labels (see unit_from_repeat_index / repeat_index_from_unit).
const std::vector<std::string> REPEAT_CHOICES = {
    "none", "days", "weeks", "months", "years", "weekdays",
};
const std::vector<std::string> REPEAT_FROM_CHOICES = {
    "due date", "completion",
};
const std::vector<std::string> WEEKDAY_CHOICES = {
    "Mo", "Tu", "We", "Th", "Fr", "Sa", "Su",
};

/** The seven weekdays in the same Monday-first order as WEEKDAY_CHOICES. */
constexpr std::array<std::chrono::weekday, 7> WEEKDAYS_MON_FIRST{{
    std::chrono::Monday,   std::chrono::Tuesday, std::chrono::Wednesday,
    std::chrono::Thursday, std::chrono::Friday,  std::chrono::Saturday,
    std::chrono::Sunday,
}};

/** Maps a "Repeat" select index (1..4) to its interval unit. */
RecurUnit unit_from_repeat_index(std::size_t index) {
    switch(index) {
        case 2:  return RecurUnit::WEEK;
        case 3:  return RecurUnit::MONTH;
        case 4:  return RecurUnit::YEAR;
        default: return RecurUnit::DAY;
    }
}

/** Maps an interval unit to its "Repeat" select index. */
std::size_t repeat_index_from_unit(RecurUnit unit) {
    switch(unit) {
        case RecurUnit::WEEK:  return 2;
        case RecurUnit::MONTH: return 3;
        case RecurUnit::YEAR:  return 4;
        case RecurUnit::DAY:   break;
    }
    return 1;
}

/**
 * Assembles a rule from the wizard's parts. Mode 0 (none) and the weekday mode
 * with no day picked both yield no recurrence; modes 1..4 build an interval
 * rule, mode 5 a (Monday-first sorted) weekday rule.
 */
std::optional<RecurrenceRule> make_recurrence(
    std::size_t mode, int count, std::vector<std::chrono::weekday> weekdays,
    RecurBasis basis
) {
    if(mode == 5) {
        if(weekdays.empty()) {
            return std::nullopt;
        }
        std::ranges::sort(weekdays, {}, [](std::chrono::weekday day) {
            return day.iso_encoding();
        });
        return RecurrenceRule{
            .unit = RecurUnit::DAY, .interval = 1,
            .weekdays = std::move(weekdays), .basis = basis
        };
    }
    if(mode >= 1 && mode <= 4) {
        return RecurrenceRule{
            .unit = unit_from_repeat_index(mode),
            .interval = std::max(1, count), .weekdays = {}, .basis = basis
        };
    }
    return std::nullopt;
}

/** Seeds the date picker from a calendar day (the value fields only). */
std::tm to_tm(std::chrono::year_month_day date) {
    std::tm seed{};
    seed.tm_year = static_cast<int>(date.year()) - 1900;
    seed.tm_mon  = static_cast<int>(static_cast<unsigned>(date.month())) - 1;
    seed.tm_mday = static_cast<int>(static_cast<unsigned>(date.day()));
    return seed;
}

/** Converts a picked `std::tm` to a calendar day, or nullopt if implausible. */
std::optional<std::chrono::year_month_day> from_tm(const std::tm& picked) {
    const std::chrono::year_month_day date{
        std::chrono::year{picked.tm_year + 1900},
        std::chrono::month{static_cast<unsigned>(picked.tm_mon + 1)},
        std::chrono::day{static_cast<unsigned>(picked.tm_mday)},
    };
    if(!date.ok()) {
        return std::nullopt;
    }
    return date;
}

/** Resolves the editor for the multiline field: config, then $EDITOR, nvim. */
std::string resolve_editor(const Config& config) {
    if(!config.editor.empty()) {
        return config.editor;
    }
    if(const char* from_env = std::getenv("EDITOR")) {
        return from_env;
    }
    return "nvim";
}

/** The fields collected from a task form. */
struct FormResult {
    std::string title;
    std::string description;
    std::optional<std::chrono::year_month_day> due;
    Priority priority = Priority::NONE;
    bool someday = false;
    Status status = Status::OPEN;
    bool note = false;
    std::optional<RecurrenceRule> recurrence;
};

/** Outcome of the recurrence wizard (changed=false when the user cancels). */
struct RecurChoice {
    bool changed = false;
    std::optional<RecurrenceRule> rule;
};

/** A rounded, full-width purple frame shared by the wizard's pickers. */
sparcli::SelectOpts wizard_select_opts(const char* prompt) {
    return sparcli::SelectOpts{
        .prompt = prompt,
        .accent = sparcli::palette::purple(),
        .box = {
            .enabled = true,
            .border = {.type = SC_BORDER_ROUNDED,
                       .color = sparcli::palette::purple()},
            .width_mode = SC_WIDTH_FULL,
        },
    };
}

/**
 * Step-by-step recurrence editor: asks only the inputs that apply to the chosen
 * mode (a count for intervals, a weekday set for weekdays, then the basis).
 * Each step is seeded from `current`; cancelling any step keeps `current`.
 *
 * @param current The rule being edited (std::nullopt = not recurring).
 * @return The new rule (or std::nullopt for "none"), or changed=false on cancel.
 */
RecurChoice run_recurrence_wizard(const std::optional<RecurrenceRule>& current) {
    // Seed mode / count / weekday set / basis from the current rule.
    std::size_t mode_seed = 0;
    int count_seed = 1;
    std::vector<std::size_t> weekday_seed;
    std::size_t basis_seed = 0;
    if(current) {
        basis_seed = current->basis == RecurBasis::COMPLETION ? 1 : 0;
        if(!current->weekdays.empty()) {
            mode_seed = 5;
            for(const auto& day : current->weekdays) {
                weekday_seed.push_back(day.iso_encoding() - 1);
            }
        } else {
            mode_seed = repeat_index_from_unit(current->unit);
            count_seed = current->interval;
        }
    }

    // 1) Mode.
    sparcli::Select mode_pick(wizard_select_opts("Repeat"));
    for(const auto& choice : REPEAT_CHOICES) {
        mode_pick.add(choice);
    }
    mode_pick.set_cursor(mode_seed);
    const auto mode = mode_pick.run_one();
    if(!mode) {
        return {};   // cancelled
    }
    if(*mode == 0) {
        return {.changed = true, .rule = std::nullopt};   // explicit "none"
    }

    // 2) The input that applies to the chosen mode.
    int count = count_seed;
    std::vector<std::chrono::weekday> weekdays;
    if(*mode == 5) {
        sparcli::SelectOpts day_opts =
            wizard_select_opts("Weekdays (space toggles, enter confirms)");
        day_opts.multi = true;
        sparcli::Select day_pick(day_opts);
        for(const auto& label : WEEKDAY_CHOICES) {
            day_pick.add(label);
        }
        for(const auto index : weekday_seed) {
            day_pick.set_checked(index, true);
        }
        const auto picked = day_pick.run();
        if(!picked) {
            return {};   // cancelled
        }
        for(const auto index : *picked) {
            if(index < WEEKDAYS_MON_FIRST.size()) {
                weekdays.push_back(WEEKDAYS_MON_FIRST[index]);
            }
        }
    } else {
        const auto value = sparcli::number_input(
            "Every how many " + REPEAT_CHOICES[*mode] + "?",
            {.initial = static_cast<double>(count_seed), .min = 1, .max = 999,
             .decimals = 0,
             .box = {.enabled = true,
                     .border = {.type = SC_BORDER_ROUNDED,
                                .color = sparcli::palette::purple()},
                     .width_mode = SC_WIDTH_FULL}}
        );
        if(!value) {
            return {};   // cancelled
        }
        count = std::max(1, static_cast<int>(std::lround(*value)));
    }

    // 3) Basis (skipped only for "none", handled above).
    sparcli::Select basis_pick(wizard_select_opts("Repeat from"));
    for(const auto& choice : REPEAT_FROM_CHOICES) {
        basis_pick.add(choice);
    }
    basis_pick.set_cursor(basis_seed);
    const auto basis_index = basis_pick.run_one();
    if(!basis_index) {
        return {};   // cancelled
    }
    const RecurBasis basis =
        *basis_index == 1 ? RecurBasis::COMPLETION : RecurBasis::DUE;

    return {.changed = true,
            .rule = make_recurrence(*mode, count, std::move(weekdays), basis)};
}

/**
 * Builds and runs the task/note form, seeded from `existing` when editing.
 *
 * A note layout (Title, Note, Description) is used when the item is a note;
 * otherwise the full task layout. `note_default` seeds the Note checkbox for a
 * new item.
 *
 * @return The entered fields, or std::nullopt when the user cancels/discards.
 */
std::optional<FormResult> run_task_form(
    const Config& config, const Task* existing, bool note_default
) {
    const bool is_note = existing ? existing->note : note_default;
    const std::string editor = resolve_editor(config);

    // Editable state, seeded from `existing`; the form is rebuilt from this on
    // each pass so the recurrence wizard (Ctrl-R) can refresh its summary while
    // the other fields keep their current values.
    FormResult state;
    state.note = is_note;
    if(existing) {
        state.title = existing->title;
        state.description = existing->description;
        state.due = existing->due;
        state.priority = existing->priority;
        state.someday = existing->someday;
        state.status = existing->status;
        state.recurrence = existing->recurrence;
    }

    // The pinned header gives the same shell as the finder; it is borrowed by
    // run(), so it must outlive every rebuild of the form.
    sparcli::Text head;
    head.append(
        "mdtask", sparcli::style(SC_TEXT_ATTR_BOLD, sparcli::palette::purple())
    );
    head.append("  ", sparcli::style(SC_TEXT_ATTR_DIM));
    if(existing) {
        // A fixed label, not the title (a long title would wrap the header).
        head.append(
            is_note ? "editing note" : "editing task",
            sparcli::style(SC_TEXT_ATTR_BOLD, sparcli::palette::yellow())
        );
        // Read-only note: when this task was completed.
        if(const std::string completed = presentation::format_completed(
               existing->completed_at, config.date_format
           ); !completed.empty()) {
            head.append(
                "  \xc2\xb7  completed " + completed,
                sparcli::style(SC_TEXT_ATTR_NONE, sparcli::palette::green())
            );
        }
    } else {
        head.append(
            is_note ? "new note" : "new task",
            sparcli::style(SC_TEXT_ATTR_BOLD, sparcli::palette::green())
        );
    }
    const sparcli::Rendered header = presentation::app_header(head);

    // A note's grid is single-column; a task's title/description span all five.
    const int wide_span = is_note ? 1 : 5;
    constexpr int ACT_EDIT_RECURRENCE = 1;

    bool dirty = false;   // any edit (a field or the wizard) across all passes
    bool first = true;
    for(;;) {
        sparcli::FormOpts opts{
            // Magenta highlights the active cell and the inline editor panel.
            .accent = sparcli::palette::magenta(),
            .hide_summary = true,          // no "Form saved" line after submit
            // Open the title editor right away only on the first pass of a new
            // item; reopening after the wizard starts in navigation mode.
            .autoedit = existing == nullptr && first,
            .editor = editor.c_str(),
            .editor_suffix = ".md",        // temp file gets a .md extension
            .fullscreen = true,            // share the finder's alternate screen
            .valign = SC_VALIGN_MIDDLE,
            .valign_scope = SC_VALIGN_SCOPE_CONTENT,
            .header = header.get(),
            .modified_marker = "[*] ",     // flag changed fields in their title
        };
        // Ctrl-R ends this run to open the recurrence wizard (tasks only); the
        // loop then reopens the form with the refreshed Repeat summary.
        sparcli::Shortcuts shortcuts;
        if(!is_note) {
            shortcuts.on_return(
                sparcli::key_ctrl('r'), ACT_EDIT_RECURRENCE, "repeat"
            );
        }
        shortcuts.apply(opts);

        // A zeroed seed leaves the due field empty; a set date pre-selects it.
        const std::tm due_initial = state.due ? to_tm(*state.due) : std::tm{};

        sparcli::Form form(opts);

        form.row_begin();
        const int title_id = form.add_text(
            "Title", state.title,
            {.width_mode = SC_FWIDTH_AUTO, .col_span = wide_span,
             .required = true}
        );

        // Task-only fields: priority, due, someday, status.
        int priority_id = -1;
        int due_id = -1;
        int someday_id = -1;
        int status_id = -1;
        if(!is_note) {
            form.row_begin();
            priority_id = form.add_select(
                "Priority", PRIORITY_CHOICES, priority_to_index(state.priority),
                {.width_mode = SC_FWIDTH_PCT, .width = 20}
            );
            due_id = form.add_date(
                "Due", due_initial,
                {.width_mode = SC_FWIDTH_PCT, .width = 20, .date_optional = true,
                 .help = "enter picks a date, del clears it"}
            );
            someday_id = form.add_bool(
                "Someday", state.someday,
                {.width_mode = SC_FWIDTH_PCT, .width = 20}
            );
            status_id = form.add_select(
                "Status", STATUS_CHOICES, status_to_index(state.status),
                {.width_mode = SC_FWIDTH_PCT, .width = 20}
            );
        }

        // In the note layout the checkbox gets its own full-width row; on the
        // task row it is the fifth column (PCT to line up with the other four).
        sparcli::FieldOpts note_opts{};
        if(is_note) {
            form.row_begin();
            note_opts.width_mode = SC_FWIDTH_AUTO;
        } else {
            note_opts.width_mode = SC_FWIDTH_PCT;
            note_opts.width = 20;
        }
        const int note_id = form.add_bool("Note", state.note, note_opts);

        // A single, display-only Repeat summary (task only): read-only and not
        // selectable, so the cursor skips it and the wizard (Ctrl-R) is the only
        // way to change it.
        if(!is_note) {
            std::string repeat_summary = "none";
            if(state.recurrence) {
                repeat_summary = format_recurrence(*state.recurrence);
                if(state.recurrence->basis == RecurBasis::COMPLETION) {
                    repeat_summary += "  \xc2\xb7  from completion";
                }
            }
            form.row_begin();
            static_cast<void>(form.add_text(
                "Repeat", repeat_summary,
                {.width_mode = SC_FWIDTH_AUTO, .col_span = wide_span,
                 .help = "Ctrl-R to edit recurrence",
                 .read_only = true, .not_selectable = true}
            ));
        }

        form.row_begin();
        const int description_id = form.add_text(
            "Description", state.description,
            {.width_mode = SC_FWIDTH_AUTO, .col_span = wide_span, .height = 6,
             .multiline = true, .help = "ctrl-g opens the editor"}
        );

        const bool submitted = form.run();
        // A RETURN shortcut also ends run() with SC_INPUT_OK, so check which
        // chord fired before treating a non-cancel exit as a submit.
        const int fired = shortcuts.fired();
        dirty = dirty || form.modified();   // accumulate across rebuilds

        // Read the live field values back into state before the form is gone.
        state.title = std::string(form.get_string(title_id));
        state.description = std::string(form.get_string(description_id));
        state.note = form.get_bool(note_id);
        if(!is_note) {
            state.priority = priority_from_index(form.get_choice(priority_id));
            state.someday = form.get_bool(someday_id);
            state.status = status_from_index(form.get_choice(status_id));
            const auto picked = form.get_date(due_id);
            state.due = (picked && !sparcli::date_empty(*picked))
                ? from_tm(*picked)
                : std::nullopt;
        }

        if(fired == ACT_EDIT_RECURRENCE) {
            const RecurChoice choice = run_recurrence_wizard(state.recurrence);
            if(choice.changed) {
                state.recurrence = choice.rule;
                dirty = true;
            }
            first = false;
            continue;   // rebuild with the refreshed summary
        }
        if(submitted) {
            break;
        }
        // Esc: discard a pristine form silently; otherwise ask (default save).
        if(!dirty) {
            return std::nullopt;
        }
        if(!sparcli::confirm("Save changes?", {.default_yes = true})
                .value_or(false)) {
            return std::nullopt;
        }
        break;
    }
    return state;
}

}  // namespace

std::optional<Task> run_new_task_form(
    TaskService& service, const Config& config, bool note
) {
    const auto fields = run_task_form(config, nullptr, note);
    if(!fields) {
        return std::nullopt;
    }

    const auto created = service.add_task({
        .title       = fields->title,
        .description = fields->description,
        .due         = fields->due,
        .priority    = fields->priority,
        .someday     = fields->someday,
        .note        = fields->note,
        .recurrence  = fields->recurrence,
    });
    if(!created) {
        sparcli::alert::warning(created.error().message);
        return std::nullopt;
    }
    // A new task starts open; honor a non-default status chosen in the form
    // (notes have no status).
    if(!fields->note && fields->status != Status::OPEN) {
        if(const auto updated = service.set_status(created->id, fields->status)) {
            return *updated;
        }
    }
    return *created;
}

std::optional<Task> run_edit_task_form(
    TaskService& service, const Config& config, const Task& task
) {
    const auto fields = run_task_form(config, &task, task.note);
    if(!fields) {
        return std::nullopt;
    }

    Task updated = task;
    updated.title = fields->title;
    updated.description = fields->description;
    updated.due = fields->due;
    updated.priority = fields->priority;
    updated.someday = fields->someday;
    updated.status = fields->status;
    updated.note = fields->note;
    updated.recurrence = fields->recurrence;

    const auto saved = service.update_task(updated);
    if(!saved) {
        sparcli::alert::warning(saved.error().message);
        return std::nullopt;
    }
    return *saved;
}

}  // namespace mdtask
