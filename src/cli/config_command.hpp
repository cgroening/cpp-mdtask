#pragma once

#include "cli/command.hpp"
#include "config/config.hpp"

#include <string_view>

namespace mdtask {

/**
 * "config" subcommand: shows the resolved configuration and file locations.
 *
 * Output example: a sparcli key/value view of every path and setting the
 * application uses, so users can see where their data, config and log
 * files live (XDG base directories).
 */
class ConfigCommand : public Command {
public:
    /**
     * Creates the command.
     *
     * @param config The resolved application configuration to display.
     */
    explicit ConfigCommand(const Config& config);

    [[nodiscard]] std::string_view name() const override;
    [[nodiscard]] std::string_view summary() const override;
    void configure(sparcli::ArgsCmd command_node) override;
    [[nodiscard]] int run(const sparcli::Args& args) override;

private:
    const Config& config_;
};

}  // namespace mdtask
