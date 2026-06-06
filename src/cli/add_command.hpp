#pragma once

#include "cli/command.hpp"
#include "service/task_service.hpp"

#include <string_view>

namespace mdtask {

/**
 * "add" subcommand: creates a task from a title argument or a prompt.
 *
 * Input example: when no TITLE argument is given, the title is asked for
 * interactively with sparcli's text_input widget.
 */
class AddCommand : public Command {
public:
    /**
     * Creates the command.
     *
     * @param service Task service used to create the task.
     */
    explicit AddCommand(TaskService& service);

    [[nodiscard]] std::string_view name() const override;
    [[nodiscard]] std::string_view summary() const override;
    void configure(sparcli::ArgsCmd command_node) override;
    [[nodiscard]] int run(const sparcli::Args& args) override;

private:
    TaskService& service_;
};

}  // namespace mdtask
