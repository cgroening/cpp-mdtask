#include "cli/completion_command.hpp"

namespace mdtask {

std::string_view CompletionCommand::name() const {
    return "completion";
}

std::string_view CompletionCommand::summary() const {
    return "Print the zsh completion script";
}

void CompletionCommand::configure(sparcli::ArgsCmd command_node) {
    // No arguments: the command only prints the generated script.
    (void)command_node;
}

int CompletionCommand::run(const sparcli::Args& args) {
    args.print_zsh_completion();
    return 0;
}

}  // namespace mdtask
