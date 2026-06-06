#include "config/config_loader.hpp"

#include "util/app_info.hpp"

#include <app/sparcli_config.hpp>
#include <sparcli.hpp>

#include <filesystem>
#include <map>
#include <string>

namespace mdtask {

namespace {

/** Maps the textual log levels accepted in the config file to sparcli's. */
const std::map<std::string, ScLogLevel, std::less<>> LOG_LEVELS = {
    {"debug", SC_LOG_DEBUG},
    {"info",  SC_LOG_INFO},
    {"warn",  SC_LOG_WARN},
    {"error", SC_LOG_ERROR},
    {"off",   SC_LOG_OFF},
};

/** Maps the textual date formats accepted in the config file. */
const std::map<std::string, DateFormat, std::less<>> DATE_FORMATS = {
    {"dmy", DateFormat::DMY},
    {"iso", DateFormat::ISO},
};

/** Default tasks directory: `$XDG_DATA_HOME/mdtask/tasks` (or `./tasks`). */
std::string default_tasks_dir() {
    if(const auto dir = sparcli::paths::dir(SC_PATH_DATA, APP_NAME)) {
        return (std::filesystem::path(*dir) / "tasks").string();
    }
    return "tasks";
}

/**
 * Builds the layered config: built-in defaults, then the optional TOML file,
 * then environment overrides under the MDTASK_ prefix.
 */
Result<sparcli::Config> build_layered_config() {
    auto defaults = sparcli::serde::Value::object();
    defaults.set("tasks_dir", sparcli::serde::Value::string(
        default_tasks_dir()
    ));

    const auto log_file = sparcli::paths::file(
        SC_PATH_STATE, APP_NAME, std::string(APP_NAME) + ".log"
    );
    defaults.set("log_file", sparcli::serde::Value::string(
        log_file ? *log_file : ""
    ));

    defaults.set("log_level", sparcli::serde::Value::string("warn"));
    defaults.set("date_format", sparcli::serde::Value::string("dmy"));

    sparcli::Config config;
    config.set_defaults(defaults);

    // A missing file is the normal first-run case; a malformed one is an error
    // the user should see and fix rather than have silently ignored.
    if(const auto path =
           sparcli::paths::file(SC_PATH_CONFIG, APP_NAME, "config.toml")) {
        ScParseError error{};
        if(config.load_file(*path, &error) == SC_CONFIG_ERROR) {
            return std::unexpected(config_error(
                "Invalid config " + *path + ": " + error.message,
                "Fix the syntax or remove the file to use defaults"
            ));
        }
    }

    // Environment overrides, e.g. MDTASK_TASKS_DIR or MDTASK_LOG_LEVEL.
    config.load_env("MDTASK_");

    return config;
}

}  // namespace

Result<Config> load_config() {
    auto layered = build_layered_config();
    if(!layered) {
        return std::unexpected(layered.error());
    }

    const std::string level_name = layered->get_string("log_level", "warn");
    const auto level = LOG_LEVELS.find(level_name);
    if(level == LOG_LEVELS.end()) {
        return std::unexpected(config_error(
            "Invalid log_level '" + level_name + "' in config",
            "Valid levels: debug, info, warn, error, off"
        ));
    }

    const std::string format_name = layered->get_string("date_format", "dmy");
    const auto format = DATE_FORMATS.find(format_name);
    if(format == DATE_FORMATS.end()) {
        return std::unexpected(config_error(
            "Invalid date_format '" + format_name + "' in config",
            "Valid formats: dmy, iso"
        ));
    }

    const std::filesystem::path tasks_dir = layered->get_string("tasks_dir");
    // The archive defaults to a subfolder of the resolved tasks directory, so
    // a custom tasks_dir keeps its archive alongside it unless overridden.
    std::filesystem::path archive_dir = layered->get_string("archive_dir");
    if(archive_dir.empty()) {
        archive_dir = tasks_dir / "archive";
    }

    return Config{
        .log_level   = level->second,
        .tasks_dir   = tasks_dir,
        .archive_dir = archive_dir,
        .log_file    = layered->get_string("log_file"),
        .date_format = format->second,
        .editor      = layered->get_string("editor"),
    };
}

}  // namespace mdtask
