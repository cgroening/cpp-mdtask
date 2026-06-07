#include "cli/done_command.hpp"

#include "cli/error_output.hpp"
#include "domain/task.hpp"

#include <sparcli.hpp>

#include <optional>
#include <string>
#include <vector>

namespace mdtask {

namespace {

/**
 * Input example: lets the user pick one open task from a Select list.
 *
 * Returns the id of the chosen task, or std::nullopt when the user cancels
 * or no terminal is available.
 */
std::optional<std::string> pick_task_interactively(
    const std::vector<Task>& open_tasks
) {
    sparcli::Select select({.prompt = "Which task is done?"});
    for(const auto& task : open_tasks) {
        select.add("[" + task.id + "] " + task.title);
    }

    const auto picked = select.run_one();
    if(!picked) {
        return std::nullopt;
    }
    return open_tasks[*picked].id;
}

/**
 * Takes the task id from the ID argument, or lets the user pick one
 * interactively when the command was invoked without it.
 */
std::optional<std::string> resolve_id(
    const sparcli::Args& args, TaskService& service
) {
    if(auto id = args.get_str("ID")) {
        return id;
    }

    const auto open_tasks = service.open_tasks();
    if(open_tasks.empty()) {
        return std::nullopt;
    }
    return pick_task_interactively(open_tasks);
}

}  // namespace

DoneCommand::DoneCommand(TaskService& service) : service_(service) {}

std::string_view DoneCommand::name() const {
    return "done";
}

std::string_view DoneCommand::summary() const {
    return "Toggle a task's done state (picks interactively without an id)";
}

void DoneCommand::configure(sparcli::ArgsCmd command_node) {
    command_node.positional(
        "ID", SC_ARG_STR, "Id of the task to complete", false
    );
}

int DoneCommand::run(const sparcli::Args& args) {
    const auto id = resolve_id(args, service_);
    if(!id) {
        sparcli::alert::warning("Nothing to do: no open task selected.");
        return 1;
    }

    const auto task = service_.toggle_done(*id);
    if(!task) {
        return report_error(task.error());
    }

    const std::string label =
        task->status == Status::DONE ? "Done   " : "Reopened";
    sparcli::print(
        label + " ", sparcli::style(SC_TEXT_ATTR_BOLD, sparcli::green())
    );
    sparcli::println("[" + task->id + "] " + task->title);
    return 0;
}

}  // namespace mdtask
