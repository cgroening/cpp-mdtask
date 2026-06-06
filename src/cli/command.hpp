#pragma once

#include <sparcli.hpp>

#include <string_view>

namespace mdtask {

/**
 * One CLI subcommand (the Command pattern).
 *
 * Each concrete command receives its dependencies through its constructor
 * and is wired into the application in two phases:
 *
 *   1. configure() declares its flags, options, positionals and nested
 *      subcommands on the parser node created for it.
 *   2. run() reads the parsed, typed values from the Args object and
 *      translates them into service calls; it renders results via sparcli.
 *
 * Commands hold no business logic, so new subcommands can be added without
 * touching the existing ones.
 */
class Command {
public:
    virtual ~Command() = default;

    /** The word that selects this command on the command line, e.g. "add". */
    [[nodiscard]] virtual std::string_view name() const = 0;

    /** One-line description shown in the help screen. */
    [[nodiscard]] virtual std::string_view summary() const = 0;

    /**
     * Declares this command's arguments on its parser node.
     *
     * @param command_node The parser node created for this command; add
     *                     flags, options, positionals and nested
     *                     subcommands here. The application sets this node's
     *                     userdata to the command so dispatch finds it; a
     *                     command that registers nested subcommands must set
     *                     each child node's userdata itself (see DemoCommand).
     */
    virtual void configure(sparcli::ArgsCmd command_node) = 0;

    /**
     * Executes the command with parsed arguments.
     *
     * @param args The parser after a successful parse; read typed values
     *             (get_str, get_flag, ...) and the selected leaf command
     *             from it.
     * @return Process exit code (0 = success).
     */
    [[nodiscard]] virtual int run(const sparcli::Args& args) = 0;
};

}  // namespace mdtask
