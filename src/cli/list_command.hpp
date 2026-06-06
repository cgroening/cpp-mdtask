#pragma once

#include "cli/command.hpp"
#include "service/task_service.hpp"

#include <string_view>

namespace mdtask {

/**
 * "list" subcommand: renders all tasks.
 *
 * Output example: the default format renders a sparcli Table with styled
 * cells; `--format plain` prints one task per line for piping into other
 * tools. The format option demonstrates choices + defaults on the parser.
 */
class ListCommand : public Command {
public:
    /**
     * Creates the command.
     *
     * @param service Task service used to read the tasks.
     */
    explicit ListCommand(TaskService& service);

    [[nodiscard]] std::string_view name() const override;
    [[nodiscard]] std::string_view summary() const override;
    void configure(sparcli::ArgsCmd command_node) override;
    [[nodiscard]] int run(const sparcli::Args& args) override;

private:
    TaskService& service_;
};

}  // namespace mdtask
