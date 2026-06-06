#include "cli/add_command.hpp"

#include "cli/error_output.hpp"
#include "domain/task.hpp"
#include "service/task_service.hpp"
#include "util/date.hpp"

#include <sparcli.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace mdtask {

namespace {

/** Maps a --priority value to a Priority (unknown values map to NONE). */
Priority priority_from_arg(std::string_view text) {
    if(text == "low")    { return Priority::LOW; }
    if(text == "medium") { return Priority::MEDIUM; }
    if(text == "high")   { return Priority::HIGH; }
    return Priority::NONE;
}

/**
 * Takes the title from the TITLE argument, or prompts for one interactively
 * when the command was invoked without it.
 */
std::optional<std::string> resolve_title(const sparcli::Args& args) {
    if(auto title = args.get_str("TITLE")) {
        return title;
    }
    return sparcli::text_input(
        "Task title", {.placeholder = "e.g. Buy milk"}
    );
}

}  // namespace

AddCommand::AddCommand(TaskService& service) : service_(service) {}

std::string_view AddCommand::name() const {
    return "add";
}

std::string_view AddCommand::summary() const {
    return "Add a new task (prompts for a title if none is given)";
}

void AddCommand::configure(sparcli::ArgsCmd command_node) {
    command_node.positional(
        "TITLE", SC_ARG_STR, "Title of the new task", false
    );
    command_node.opt("due", 'd', SC_ARG_STR, "DATE", "Due date (YYYY-MM-DD)");
    command_node
        .opt("priority", 'p', SC_ARG_STR, "LEVEL", "Priority level")
        .opt_choices("priority", {"none", "low", "medium", "high"});
    command_node.opt(
        "description", 'm', SC_ARG_STR, "TEXT", "Description body"
    );
    command_node.flag("someday", 's', "Mark a dateless task as someday");
}

int AddCommand::run(const sparcli::Args& args) {
    const auto title = resolve_title(args);
    if(!title) {
        sparcli::alert::warning("Cancelled: no title provided.");
        return 1;
    }

    NewTask fields;
    fields.title = *title;
    fields.priority =
        priority_from_arg(args.get_str("priority").value_or("none"));
    fields.someday = args.get_flag("someday");
    if(const auto description = args.get_str("description")) {
        fields.description = *description;
    }
    if(const auto due_text = args.get_str("due")) {
        fields.due = parse_iso_date(*due_text);
        if(!fields.due) {
            return report_error(
                validation_error("--due must be a date in YYYY-MM-DD form")
            );
        }
    }

    const auto task = service_.add_task(fields);
    if(!task) {
        return report_error(task.error());
    }

    sparcli::print(
        "Added ", sparcli::style(SC_TEXT_ATTR_BOLD, sparcli::green())
    );
    sparcli::println("[" + task->id + "] " + task->title);
    return 0;
}

}  // namespace mdtask
