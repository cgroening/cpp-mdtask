#include "cli/archive_command.hpp"

#include "cli/error_output.hpp"

#include <sparcli.hpp>

#include <string>

namespace mdtask {

ArchiveCommand::ArchiveCommand(TaskService& service) : service_(service) {}

std::string_view ArchiveCommand::name() const {
    return "archive";
}

std::string_view ArchiveCommand::summary() const {
    return "Archive a task (moves its file into the archive folder)";
}

void ArchiveCommand::configure(sparcli::ArgsCmd command_node) {
    command_node.positional(
        "ID", SC_ARG_STR, "Id of the task to archive", true
    );
}

int ArchiveCommand::run(const sparcli::Args& args) {
    const auto id = args.get_str("ID").value_or("");

    const auto task = service_.archive_task(id);
    if(!task) {
        return report_error(task.error());
    }

    sparcli::print(
        "Archived ", sparcli::style(SC_TEXT_ATTR_BOLD, sparcli::green())
    );
    sparcli::println("[" + task->id + "] " + task->title);
    return 0;
}

}  // namespace mdtask
