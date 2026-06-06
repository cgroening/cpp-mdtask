#pragma once

#include "cli/command.hpp"
#include "config/config.hpp"
#include "service/task_service.hpp"

#include <string_view>

namespace mdtask {

/**
 * "edit" subcommand: opens the interactive form for a task by id.
 */
class EditCommand : public Command {
public:
    /**
     * Creates the command.
     *
     * @param service Task service used to look up and update the task.
     * @param config  Provides the editor for the form's description field.
     */
    EditCommand(TaskService& service, const Config& config);

    [[nodiscard]] std::string_view name() const override;
    [[nodiscard]] std::string_view summary() const override;
    void configure(sparcli::ArgsCmd command_node) override;
    [[nodiscard]] int run(const sparcli::Args& args) override;

private:
    TaskService& service_;
    const Config& config_;
};

}  // namespace mdtask
