#include "tui/task_form.hpp"

#include "tui/task_presentation.hpp"

#include <sparcli.hpp>

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <optional>
#include <string>
#include <vector>

namespace mdtask {

namespace {

const std::vector<std::string> PRIORITY_CHOICES = {
    "none", "low", "medium", "high",
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
    Priority priority;
    bool someday;
};

/**
 * Builds and runs the task form, seeded from `existing` when editing.
 *
 * @return The entered fields, or std::nullopt when the user cancels.
 */
std::optional<FormResult> run_task_form(
    const Config& config, const Task* existing
) {
    const std::string editor = resolve_editor(config);
    // A zeroed seed leaves the field empty (no due date) and opens the picker
    // at today; an existing due date pre-selects that day.
    const std::tm due_initial =
        (existing && existing->due) ? to_tm(*existing->due) : std::tm{};

    // The pinned header gives the same shell as the finder; it is borrowed by
    // run(), so it must outlive the form.
    sparcli::Text head;
    head.append(
        "mdtask", sparcli::style(SC_TEXT_ATTR_BOLD, sparcli::palette::purple())
    );
    head.append("  ", sparcli::style(SC_TEXT_ATTR_DIM));
    if(existing) {
        head.append(
            "editing: ",
            sparcli::style(SC_TEXT_ATTR_BOLD, sparcli::palette::yellow())
        );
        head.append(
            existing->title,
            sparcli::style(SC_TEXT_ATTR_BOLD, sparcli::palette::purple())
        );
    } else {
        head.append(
            "new task",
            sparcli::style(SC_TEXT_ATTR_BOLD, sparcli::palette::green())
        );
    }
    const sparcli::Rendered header = presentation::app_header(head);

    sparcli::Form form({
        // Magenta highlights the active cell and the inline editor panel.
        .accent = sparcli::palette::magenta(),
        .hide_summary = true,          // no "Form saved" line after submit
        // For a new task, open the title editor right away; when editing an
        // existing task, start in navigation mode.
        .autoedit = existing == nullptr,
        .editor = editor.c_str(),
        .fullscreen = true,            // share the finder's alternate screen
        .valign = SC_VALIGN_TOP,
        .header = header.get(),
    });

    form.row_begin();
    const int title_id = form.add_text(
        "Title", existing ? existing->title : "",
        {.width_mode = SC_FWIDTH_AUTO, .col_span = 3, .required = true}
    );

    form.row_begin();
    const int priority_id = form.add_select(
        "Priority", PRIORITY_CHOICES,
        existing ? priority_to_index(existing->priority) : 0,
        {.width_mode = SC_FWIDTH_PCT, .width = 33}
    );
    const int due_id = form.add_date(
        "Due", due_initial,
        {.width_mode = SC_FWIDTH_PCT, .width = 34, .date_optional = true,
         .help = "enter picks a date, del clears it"}
    );
    const int someday_id = form.add_bool(
        "Someday", existing ? existing->someday : false,
        {.width_mode = SC_FWIDTH_AUTO}
    );

    form.row_begin();
    const int description_id = form.add_text(
        "Description", existing ? existing->description : "",
        {.width_mode = SC_FWIDTH_AUTO, .col_span = 3, .height = 6,
         .multiline = true, .help = "ctrl-g opens the editor"}
    );

    if(!form.run()) {
        return std::nullopt;
    }

    FormResult result;
    result.title = std::string(form.get_string(title_id));
    result.description = std::string(form.get_string(description_id));
    result.priority = priority_from_index(form.get_choice(priority_id));
    result.someday = form.get_bool(someday_id);

    // An empty / cleared date field means "no due date" (an Inbox task).
    if(const auto picked = form.get_date(due_id);
       picked && !sparcli::date_empty(*picked)) {
        result.due = from_tm(*picked);
    }
    return result;
}

}  // namespace

std::optional<Task> run_new_task_form(
    TaskService& service, const Config& config
) {
    const auto fields = run_task_form(config, nullptr);
    if(!fields) {
        return std::nullopt;
    }

    const auto created = service.add_task({
        .title       = fields->title,
        .description = fields->description,
        .due         = fields->due,
        .priority    = fields->priority,
        .someday     = fields->someday,
    });
    if(!created) {
        sparcli::alert::warning(created.error().message);
        return std::nullopt;
    }
    return *created;
}

std::optional<Task> run_edit_task_form(
    TaskService& service, const Config& config, const Task& task
) {
    const auto fields = run_task_form(config, &task);
    if(!fields) {
        return std::nullopt;
    }

    Task updated = task;
    updated.title = fields->title;
    updated.description = fields->description;
    updated.due = fields->due;
    updated.priority = fields->priority;
    updated.someday = fields->someday;

    const auto saved = service.update_task(updated);
    if(!saved) {
        sparcli::alert::warning(saved.error().message);
        return std::nullopt;
    }
    return *saved;
}

}  // namespace mdtask
