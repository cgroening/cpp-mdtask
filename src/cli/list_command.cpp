#include "cli/list_command.hpp"

#include "domain/task.hpp"
#include "util/date.hpp"

#include <sparcli.hpp>

#include <print>
#include <string>
#include <string_view>
#include <vector>

namespace mdtask {

namespace {

/** Serialized priority name for listings. */
std::string_view priority_name(Priority priority) {
    switch(priority) {
        case Priority::LOW:    return "low";
        case Priority::MEDIUM: return "medium";
        case Priority::HIGH:   return "high";
        case Priority::NONE:   break;
    }
    return "none";
}

/** Due date as ISO, or "-" when the task has none. */
std::string due_text(const Task& task) {
    return task.due ? to_iso(*task.due) : "-";
}

/** Output example: tasks as a sparcli Table with styled status cells. */
void print_table(const std::vector<Task>& tasks) {
    sparcli::Table table;
    table.add_column("Id", {.halign = SC_ALIGN_LEFT});
    table.add_column("Status", {.halign = SC_ALIGN_CENTER});
    table.add_column("Prio", {.halign = SC_ALIGN_LEFT});
    table.add_column("Due", {.halign = SC_ALIGN_LEFT});
    table.add_column("Title", {.halign = SC_ALIGN_LEFT});

    for(const auto& task : tasks) {
        const auto status = [&] {
            switch(task.status) {
                case Status::DONE:
                    return sparcli::cell_markup("[green]done[/]");
                case Status::IN_PROGRESS:
                    return sparcli::cell_markup("[yellow]in progress[/]");
                case Status::OPEN:
                    break;
            }
            return sparcli::cell_markup("[dim]open[/]");
        }();
        table.add_row({
            sparcli::cell(task.id),
            status,
            sparcli::cell(std::string(priority_name(task.priority))),
            sparcli::cell(due_text(task)),
            sparcli::cell(task.title),
        });
    }

    table.print({
        .border = {.type = SC_BORDER_ROUNDED},
        .header = {.row = true, .style = sparcli::style(SC_TEXT_ATTR_BOLD)},
    });
}

/** Output example: one task per line, suitable for piping into other tools. */
void print_plain(const std::vector<Task>& tasks) {
    for(const auto& task : tasks) {
        const char* status = task.status == Status::DONE          ? "done"
                           : task.status == Status::IN_PROGRESS   ? "in_progress"
                                                                  : "open";
        std::println(
            "{}\t{}\t{}\t{}\t{}",
            task.id,
            status,
            priority_name(task.priority),
            due_text(task),
            task.title
        );
    }
}

}  // namespace

ListCommand::ListCommand(TaskService& service) : service_(service) {}

std::string_view ListCommand::name() const {
    return "list";
}

std::string_view ListCommand::summary() const {
    return "List all tasks (use the bare command for the interactive agenda)";
}

void ListCommand::configure(sparcli::ArgsCmd command_node) {
    command_node
        .opt("format", 'f', SC_ARG_STR, "FORMAT", "Output format")
        .opt_choices("format", {"table", "plain"})
        .opt_default("format", "table");
}

int ListCommand::run(const sparcli::Args& args) {
    const auto tasks = service_.all_tasks();
    if(tasks.empty()) {
        sparcli::println("No tasks yet. Add one with: mdtask add \"...\"");
        return 0;
    }

    const auto format = args.get_str("format").value_or("table");
    if(format == "plain") {
        print_plain(tasks);
    } else {
        print_table(tasks);
    }
    return 0;
}

}  // namespace mdtask
