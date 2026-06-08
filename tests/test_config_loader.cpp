#include "config/config_loader.hpp"
#include "domain/errors.hpp"

#include "check.hpp"
#include "test_suite.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string_view>

using namespace mdtask;

namespace {

namespace fs = std::filesystem;

/** Root of the throwaway XDG tree used by every test case. */
fs::path test_root() {
    return fs::temp_directory_path() / "mdtask-config-test";
}

/** Absolute path of the config file load_config() will read. */
fs::path config_path() {
    return test_root() / "config" / "mdtask" / "config.toml";
}

/** Writes the given TOML to the config file, creating parent dirs. */
void write_config(std::string_view toml) {
    fs::create_directories(config_path().parent_path());
    std::ofstream stream(config_path(), std::ios::trunc);
    stream << toml;
}

/** Removes the config file so a test sees the first-run (defaults) case. */
void remove_config() {
    std::error_code ec;
    fs::remove(config_path(), ec);
}

}  // namespace

// load_config() layers defaults < TOML file < environment. XDG_*_HOME point at
// temp dirs so a real user config is never read and nothing is written under
// the actual home directory.
void run_config_loader_tests() {
    const fs::path root = test_root();
    setenv("XDG_CONFIG_HOME", (root / "config").c_str(), 1);
    setenv("XDG_DATA_HOME", (root / "data").c_str(), 1);
    setenv("XDG_STATE_HOME", (root / "state").c_str(), 1);
    unsetenv("MDTASK_LOG_LEVEL");
    unsetenv("MDTASK_TASKS_DIR");

    // With no file and no overrides, the built-in defaults apply.
    {
        remove_config();
        const auto config = load_config();
        CHECK(config.has_value());
        CHECK(config->log_level == SC_LOG_WARN);
        CHECK(!config->tasks_dir.empty());
        CHECK(config->date_format == DateFormat::DMY);
        CHECK(config->language == Language::ENGLISH);
        // The archive defaults to a subfolder of the resolved tasks dir.
        CHECK(config->archive_dir == config->tasks_dir / "archive");
    }

    // Recognized keys in the file override the defaults.
    {
        write_config(
            "# mdtask test config\n"
            "log_level = \"debug\"\n"
            "tasks_dir = \"/tmp/other_tasks\"\n"
            "date_format = \"iso\"\n"
            "language = \"german\"\n"
            "editor = \"vim\"\n"
        );
        const auto config = load_config();
        CHECK(config.has_value());
        CHECK(config->log_level == SC_LOG_DEBUG);
        CHECK(config->tasks_dir == "/tmp/other_tasks");
        CHECK(config->archive_dir == fs::path("/tmp/other_tasks") / "archive");
        CHECK(config->date_format == DateFormat::ISO);
        CHECK(config->language == Language::GERMAN);
        CHECK(config->editor == "vim");
    }

    // Environment variables override both defaults and the file.
    {
        write_config("tasks_dir = \"/tmp/file_tasks\"\n");
        setenv("MDTASK_LOG_LEVEL", "error", 1);
        setenv("MDTASK_TASKS_DIR", "/tmp/env_tasks", 1);
        const auto config = load_config();
        CHECK(config.has_value());
        CHECK(config->log_level == SC_LOG_ERROR);
        CHECK(config->tasks_dir == "/tmp/env_tasks");
        unsetenv("MDTASK_LOG_LEVEL");
        unsetenv("MDTASK_TASKS_DIR");
    }

    // An invalid log level value is rejected with a hint.
    {
        write_config("log_level = \"loud\"\n");
        const auto config = load_config();
        CHECK(!config.has_value());
        CHECK(config.error().code == ErrorCode::CONFIG_INVALID);
        CHECK(!config.error().hint.empty());
    }

    // An invalid date format is rejected too.
    {
        write_config("date_format = \"weird\"\n");
        const auto config = load_config();
        CHECK(!config.has_value());
        CHECK(config.error().code == ErrorCode::CONFIG_INVALID);
    }

    // An invalid language is rejected with a hint.
    {
        write_config("language = \"klingon\"\n");
        const auto config = load_config();
        CHECK(!config.has_value());
        CHECK(config.error().code == ErrorCode::CONFIG_INVALID);
        CHECK(!config.error().hint.empty());
    }

    // A malformed file is reported as a config error (fail fast on typos).
    {
        write_config("log_level = \n");
        const auto config = load_config();
        CHECK(!config.has_value());
        CHECK(config.error().code == ErrorCode::CONFIG_INVALID);
    }

    remove_config();
}
