#pragma once

#include "cli/command.hpp"
#include "service/task_service.hpp"

#include <string_view>

namespace mdtask {

/**
 * "archive" subcommand: moves a task out of the active set by id.
 */
class ArchiveCommand : public Command {
public:
    /**
     * Creates the command.
     *
     * @param service Task service used to archive the task.
     */
    explicit ArchiveCommand(TaskService& service);

    [[nodiscard]] std::string_view name() const override;
    [[nodiscard]] std::string_view summary() const override;
    void configure(sparcli::ArgsCmd command_node) override;
    [[nodiscard]] int run(const sparcli::Args& args) override;

private:
    TaskService& service_;
};

}  // namespace mdtask
