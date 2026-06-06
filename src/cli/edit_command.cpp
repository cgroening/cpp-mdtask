#include "cli/edit_command.hpp"

#include "cli/error_output.hpp"
#include "domain/errors.hpp"
#include "domain/task.hpp"
#include "tui/task_form.hpp"

#include <sparcli.hpp>

#include <algorithm>
#include <string>

namespace mdtask {

EditCommand::EditCommand(TaskService& service, const Config& config)
    : service_(service), config_(config) {}

std::string_view EditCommand::name() const {
    return "edit";
}

std::string_view EditCommand::summary() const {
    return "Edit a task in an interactive form";
}

void EditCommand::configure(sparcli::ArgsCmd command_node) {
    command_node.positional(
        "ID", SC_ARG_STR, "Id of the task to edit", true
    );
}

int EditCommand::run(const sparcli::Args& args) {
    const auto id = args.get_str("ID").value_or("");

    const auto tasks = service_.all_tasks();
    const auto found = std::ranges::find(tasks, id, &Task::id);
    if(found == tasks.end()) {
        return report_error(task_not_found_error(id));
    }

    if(!sparcli::input_available()) {
        sparcli::alert::warning("Editing needs an interactive terminal.");
        return 1;
    }

    if(const auto updated = run_edit_task_form(service_, config_, *found)) {
        sparcli::print(
            "Saved ", sparcli::style(SC_TEXT_ATTR_BOLD, sparcli::green())
        );
        sparcli::println("[" + updated->id + "] " + updated->title);
    }
    return 0;
}

}  // namespace mdtask
