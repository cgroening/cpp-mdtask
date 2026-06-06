#include "tui/task_form.hpp"

#include "util/date.hpp"

#include <sparcli.hpp>

#include <cstdlib>
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
    const std::string due_initial =
        (existing && existing->due) ? to_iso(*existing->due) : "";

    sparcli::Form form({
        .title  = existing ? "Edit task" : "New task",
        .accent = sparcli::palette::accent(),
        .editor = editor.c_str(),
    });

    form.row_begin();
    const int title_id = form.add_text(
        "Title", existing ? existing->title : "",
        {.width_mode = SC_FWIDTH_AUTO, .required = true}
    );

    form.row_begin();
    const int priority_id = form.add_select(
        "Priority", PRIORITY_CHOICES,
        existing ? priority_to_index(existing->priority) : 0,
        {.width_mode = SC_FWIDTH_PCT, .width = 33}
    );
    const int due_id = form.add_text(
        "Due (YYYY-MM-DD)", due_initial,
        {.width_mode = SC_FWIDTH_PCT, .width = 34}
    );
    const int someday_id = form.add_bool(
        "Someday", existing ? existing->someday : false,
        {.width_mode = SC_FWIDTH_AUTO}
    );

    form.row_begin();
    const int description_id = form.add_text(
        "Description", existing ? existing->description : "",
        {.width_mode = SC_FWIDTH_AUTO, .height = 6, .multiline = true,
         .help = "ctrl-g opens the editor"}
    );

    if(!form.run()) {
        return std::nullopt;
    }

    FormResult result;
    result.title = std::string(form.get_string(title_id));
    result.description = std::string(form.get_string(description_id));
    result.priority = priority_from_index(form.get_choice(priority_id));
    result.someday = form.get_bool(someday_id);

    const std::string due_text = std::string(form.get_string(due_id));
    if(!due_text.empty()) {
        result.due = parse_iso_date(due_text);
        if(!result.due) {
            sparcli::alert::warning(
                "Ignored invalid date '" + due_text + "' (use YYYY-MM-DD)"
            );
        }
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
