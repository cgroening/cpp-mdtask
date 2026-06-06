#include "cli/config_command.hpp"

#include "util/app_info.hpp"

#include <sparcli.hpp>

#include <string>

namespace mdtask {

namespace {

/** Returns the display name of a sparcli log level. */
std::string log_level_name(ScLogLevel level) {
    switch(level) {
        case SC_LOG_DEBUG: return "debug";
        case SC_LOG_INFO:  return "info";
        case SC_LOG_WARN:  return "warn";
        case SC_LOG_ERROR: return "error";
        case SC_LOG_OFF:   return "off";
    }
    return "unknown";
}

}  // namespace

ConfigCommand::ConfigCommand(const Config& config) : config_(config) {}

std::string_view ConfigCommand::name() const {
    return "config";
}

std::string_view ConfigCommand::summary() const {
    return "Show the resolved configuration and file locations";
}

void ConfigCommand::configure(sparcli::ArgsCmd command_node) {
    // No arguments: the command only displays the current state.
    (void)command_node;
}

int ConfigCommand::run(const sparcli::Args& args) {
    (void)args;

    const auto config_file =
        sparcli::paths::file(SC_PATH_CONFIG, APP_NAME, "config.toml");

    sparcli::rule(
        std::string(APP_NAME) + " configuration",
        {.type = SC_BORDER_SINGLE}
    );

    // Output example: a key/value listing rendered by sparcli.
    sparcli::Kv values;
    values.add("Version", APP_VERSION);
    values.add(
        "Config file",
        config_file ? *config_file : "(not resolvable)"
    );
    values.add("Tasks dir", config_.tasks_dir.string());
    values.add("Archive dir", config_.archive_dir.string());
    values.add(
        "Date format",
        config_.date_format == DateFormat::ISO ? "iso (YYYY-MM-DD)"
                                               : "dmy (DD.MM.YYYY)"
    );
    values.add(
        "Editor",
        config_.editor.empty() ? "(EDITOR, then nvim)" : config_.editor
    );
    values.add(
        "Log file",
        config_.log_file.empty() ? "(disabled)" : config_.log_file.string()
    );
    values.add("Log level (stderr)", log_level_name(config_.log_level));
    values.print();

    sparcli::println("");
    sparcli::println(
        "Edit the config file to override these values "
        "(see 'mdtask --help').",
        sparcli::style(SC_TEXT_ATTR_DIM)
    );
    return 0;
}

}  // namespace mdtask
