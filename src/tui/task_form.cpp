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

// The recurrence row: a "Repeat" mode, an interval count, a weekday set and the
// schedule basis. The mode index 0 is none; 1..4 are interval units; 5 is the
// weekday set (see unit_from_repeat_index / repeat_index_from_unit).
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
    // A zeroed seed leaves the field empty (no due date) and opens the picker
    // at today; an existing due date pre-selects that day.
    const std::tm due_initial =
        (existing && existing->due) ? to_tm(*existing->due) : std::tm{};

    // Recurrence seeds: derive the Repeat mode, interval, checked weekdays and
    // basis from an existing rule (all default to "no recurrence" otherwise).
    std::size_t repeat_seed = 0;
    double every_seed = 1;
    std::vector<std::size_t> weekday_seed;
    std::size_t repeat_from_seed = 0;
    if(existing && existing->recurrence) {
        const RecurrenceRule& rule = *existing->recurrence;
        repeat_from_seed = rule.basis == RecurBasis::COMPLETION ? 1 : 0;
        if(!rule.weekdays.empty()) {
            repeat_seed = 5;
            for(const auto& day : rule.weekdays) {
                weekday_seed.push_back(day.iso_encoding() - 1);
            }
        } else {
            repeat_seed = repeat_index_from_unit(rule.unit);
            every_seed = rule.interval;
        }
    }

    // The pinned header gives the same shell as the finder; it is borrowed by
    // run(), so it must outlive the form.
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

    sparcli::Form form({
        // Magenta highlights the active cell and the inline editor panel.
        .accent = sparcli::palette::magenta(),
        .hide_summary = true,          // no "Form saved" line after submit
        // For a new item, open the title editor right away; when editing,
        // start in navigation mode.
        .autoedit = existing == nullptr,
        .editor = editor.c_str(),
        .editor_suffix = ".md",        // temp file gets a .md extension for nvim
        .fullscreen = true,            // share the finder's alternate screen
        // Two layout variants to compare:
        //  (A) SC_VALIGN_TOP + Description `.fill_height = true` -> the form
        //      sits at the top and the description grows to fill the screen.
        //  (B) SC_VALIGN_MIDDLE + content scope (current) -> the header stays
        //      pinned at the very top, the hint/edit footer at the very bottom,
        //      and the fields are centered in the gap between them.
        // (A) is commented out (here and at .fill_height below); flip to switch.
        .valign = SC_VALIGN_MIDDLE,
        .valign_scope = SC_VALIGN_SCOPE_CONTENT,
        .header = header.get(),
        .modified_marker = "[*] ",     // flag changed fields in their box title
    });

    // A note's grid is single-column; a task's title/description span all five.
    const int wide_span = is_note ? 1 : 5;

    form.row_begin();
    const int title_id = form.add_text(
        "Title", existing ? existing->title : "",
        {.width_mode = SC_FWIDTH_AUTO, .col_span = wide_span, .required = true}
    );

    // Task-only fields: priority, due, someday, status.
    int priority_id = -1;
    int due_id = -1;
    int someday_id = -1;
    int status_id = -1;
    int repeat_id = -1;
    int every_id = -1;
    int weekdays_id = -1;
    int repeat_from_id = -1;
    if(!is_note) {
        form.row_begin();
        priority_id = form.add_select(
            "Priority", PRIORITY_CHOICES,
            existing ? priority_to_index(existing->priority) : 0,
            {.width_mode = SC_FWIDTH_PCT, .width = 20}
        );
        due_id = form.add_date(
            "Due", due_initial,
            {.width_mode = SC_FWIDTH_PCT, .width = 20, .date_optional = true,
             .help = "enter picks a date, del clears it"}
        );
        someday_id = form.add_bool(
            "Someday", existing ? existing->someday : false,
            {.width_mode = SC_FWIDTH_PCT, .width = 20}
        );

        status_id = form.add_select(
            "Status", STATUS_CHOICES,
            existing ? status_to_index(existing->status) : 0,
            {.width_mode = SC_FWIDTH_PCT, .width = 20}
        );
    }

    // In the note layout the checkbox gets its own full-width row; on the task
    // row it is the fifth column (PCT to line up with the other four).
    sparcli::FieldOpts note_opts{};
    if(is_note) {
        form.row_begin();
        note_opts.width_mode = SC_FWIDTH_AUTO;
    } else {
        note_opts.width_mode = SC_FWIDTH_PCT;
        note_opts.width = 20;
    }
    const int note_id = form.add_bool("Note", is_note, note_opts);

    // Recurrence row (task only): pick a Repeat mode plus its supporting input.
    // `Every` applies to days/weeks/months/years; `Weekdays` to the weekday set.
    if(!is_note) {
        form.row_begin();
        repeat_id = form.add_select(
            "Repeat", REPEAT_CHOICES, repeat_seed,
            {.width_mode = SC_FWIDTH_PCT, .width = 20}
        );
        every_id = form.add_number(
            "Every", every_seed,
            {.width_mode = SC_FWIDTH_PCT, .width = 20,
             .help = "count for days/weeks/months/years"}
        );
        // Spans two of the row's five columns (like Title/Description span all
        // five), so the column grid stays uniform with the row above instead of
        // a conflicting 40% width that would shift the other cells.
        weekdays_id = form.add_multiselect(
            "Weekdays", WEEKDAY_CHOICES, weekday_seed,
            {.width_mode = SC_FWIDTH_AUTO, .col_span = 2,
             .help = "used when Repeat = weekdays"}
        );
        repeat_from_id = form.add_select(
            "Repeat from", REPEAT_FROM_CHOICES, repeat_from_seed,
            {.width_mode = SC_FWIDTH_PCT, .width = 20}
        );
    }

    form.row_begin();
    const int description_id = form.add_text(
        "Description", existing ? existing->description : "",
        {.width_mode = SC_FWIDTH_AUTO, .col_span = wide_span, .height = 6,
         // Commented out to try the centered layout instead of the fill layout
         // (see the .valign note above); re-enable together with SC_VALIGN_TOP.
         // .fill_height = true,
         .multiline = true, .help = "ctrl-g opens the editor"}
    );

    if(!form.run()) {
        // Esc: discard a pristine form silently; otherwise ask (default save).
        if(!form.modified()) {
            return std::nullopt;
        }
        if(!sparcli::confirm("Save changes?", {.default_yes = true})
                .value_or(false)) {
            return std::nullopt;
        }
    }

    FormResult result;
    result.title = std::string(form.get_string(title_id));
    result.description = std::string(form.get_string(description_id));
    result.note = form.get_bool(note_id);

    // Task-only fields are read only when present; a note keeps the defaults.
    if(!is_note) {
        result.priority = priority_from_index(form.get_choice(priority_id));
        result.someday = form.get_bool(someday_id);
        result.status = status_from_index(form.get_choice(status_id));
        if(const auto picked = form.get_date(due_id);
           picked && !sparcli::date_empty(*picked)) {
            result.due = from_tm(*picked);
        }

        // Build the recurrence rule from the Repeat mode (0 = none).
        const std::size_t repeat_choice = form.get_choice(repeat_id);
        const RecurBasis basis = form.get_choice(repeat_from_id) == 1
            ? RecurBasis::COMPLETION
            : RecurBasis::DUE;
        if(repeat_choice == 5) {
            std::vector<std::chrono::weekday> weekdays;
            for(const auto index : form.get_checked(weekdays_id)) {
                if(index < WEEKDAYS_MON_FIRST.size()) {
                    weekdays.push_back(WEEKDAYS_MON_FIRST[index]);
                }
            }
            std::ranges::sort(weekdays, {}, [](std::chrono::weekday day) {
                return day.iso_encoding();
            });
            if(!weekdays.empty()) {   // no day picked means no recurrence
                result.recurrence = RecurrenceRule{
                    .unit = RecurUnit::DAY, .interval = 1,
                    .weekdays = std::move(weekdays), .basis = basis
                };
            }
        } else if(repeat_choice >= 1 && repeat_choice <= 4) {
            const int interval = std::max(
                1, static_cast<int>(std::lround(form.get_number(every_id)))
            );
            result.recurrence = RecurrenceRule{
                .unit = unit_from_repeat_index(repeat_choice),
                .interval = interval, .weekdays = {}, .basis = basis
            };
        }
    }
    return result;
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
