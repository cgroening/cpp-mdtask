#pragma once

#include "cli/command.hpp"
#include "service/task_service.hpp"

#include <string_view>

namespace mdtask {

/**
 * "done" subcommand: marks a task as completed.
 *
 * Input example: when no ID argument is given, the task is picked from an
 * interactive sparcli Select list of all open tasks.
 */
class DoneCommand : public Command {
public:
    /**
     * Creates the command.
     *
     * @param service Task service used to look up and update tasks.
     */
    explicit DoneCommand(TaskService& service);

    [[nodiscard]] std::string_view name() const override;
    [[nodiscard]] std::string_view summary() const override;
    void configure(sparcli::ArgsCmd command_node) override;
    [[nodiscard]] int run(const sparcli::Args& args) override;

private:
    TaskService& service_;
};

}  // namespace mdtask
