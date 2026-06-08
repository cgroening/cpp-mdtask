#include "config/config_loader.hpp"

#include "util/app_info.hpp"

#include <app/sparcli_config.hpp>
#include <sparcli.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mdtask {

namespace {

/** The two categories that must always exist, with their default labels. */
constexpr const char* NONE_CATEGORY = "-";
constexpr const char* PROJECT_CATEGORY = "Project";

/**
 * Resolves a config color token into a Color: either a `#RRGGBB` hex literal or
 * a name accepted by sparcli (the eight ANSI names and the named palette).
 *
 * @return The color, or std::nullopt when the token is neither valid hex nor a
 *         known color name.
 */
std::optional<sparcli::Color> parse_color(const std::string& token) {
    if(token.empty()) {
        return sparcli::Color{};   // zero-init = "use the default"
    }
    if(token.size() == 7 && token.front() == '#') {
        std::uint8_t rgb[3] = {0, 0, 0};
        for(int i = 0; i < 3; ++i) {
            const std::string byte = token.substr(1 + i * 2, 2);
            for(const char digit : byte) {
                if(!std::isxdigit(static_cast<unsigned char>(digit))) {
                    return std::nullopt;
                }
            }
            rgb[i] = static_cast<std::uint8_t>(std::stoi(byte, nullptr, 16));
        }
        return sparcli::rgb(rgb[0], rgb[1], rgb[2]);
    }
    return sparcli::color_by_name(token);
}

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

/** Maps the textual languages accepted in the config file. */
const std::map<std::string, Language, std::less<>> LANGUAGES = {
    {"english", Language::ENGLISH},
    {"german",  Language::GERMAN},
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
    defaults.set("language", sparcli::serde::Value::string("english"));

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

/** Built-in categories used when the config file defines none. */
std::vector<CategoryDef> default_categories() {
    return {
        {.name = NONE_CATEGORY},
        {.name = PROJECT_CATEGORY, .shortform = "P",
         .fg = sparcli::palette::purple()},
    };
}

/**
 * Reads the `categories` array from the layered config and resolves each
 * entry's colors. Guarantees that "-" is present at index 0 and "Project"
 * exists somewhere in the list (both injected when missing).
 *
 * @return The resolved categories, or a CONFIG_INVALID error on a bad color.
 */
Result<std::vector<CategoryDef>> load_categories(const sparcli::Config& layered) {
    const sparcli::serde::View array = layered.get("categories");
    std::vector<CategoryDef> categories;
    for(std::size_t i = 0; i < array.size(); ++i) {
        const sparcli::serde::View entry = array.at(i);
        const auto name = entry.get("name").as_string();
        if(!name || name->empty()) {
            continue;   // a category without a name is meaningless; skip it
        }
        CategoryDef def;
        def.name = std::string(*name);
        if(const auto s = entry.get("short").as_string()) {
            def.shortform = std::string(*s);
        }
        const std::string fg_token =
            std::string(entry.get("fg").as_string().value_or(""));
        const std::string bg_token =
            std::string(entry.get("bg").as_string().value_or(""));
        const auto fg = parse_color(fg_token);
        const auto bg = parse_color(bg_token);
        if(!fg || !bg) {
            return std::unexpected(config_error(
                "Invalid category color '" +
                    (!fg ? fg_token : bg_token) + "' for '" + def.name + "'",
                "Use a color name (e.g. blue) or a #RRGGBB hex value"
            ));
        }
        def.fg = *fg;
        def.bg = *bg;
        categories.push_back(std::move(def));
    }

    // Nothing configured: fall back to the built-in defaults.
    if(categories.empty()) {
        return default_categories();
    }

    // Enforce the two reserved categories: "-" first, "Project" present.
    const auto has = [&](const char* name) {
        return std::ranges::any_of(categories, [&](const CategoryDef& c) {
            return c.name == name;
        });
    };
    if(categories.front().name != NONE_CATEGORY) {
        std::erase_if(categories, [](const CategoryDef& c) {
            return c.name == NONE_CATEGORY;
        });
        categories.insert(categories.begin(), {.name = NONE_CATEGORY});
    }
    if(!has(PROJECT_CATEGORY)) {
        categories.push_back(
            {.name = PROJECT_CATEGORY, .shortform = "P",
             .fg = sparcli::palette::purple()}
        );
    }
    return categories;
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

    const std::string language_name =
        layered->get_string("language", "english");
    const auto language = LANGUAGES.find(language_name);
    if(language == LANGUAGES.end()) {
        return std::unexpected(config_error(
            "Invalid language '" + language_name + "' in config",
            "Valid languages: english, german"
        ));
    }

    const std::filesystem::path tasks_dir = layered->get_string("tasks_dir");
    // The archive defaults to a subfolder of the resolved tasks directory, so
    // a custom tasks_dir keeps its archive alongside it unless overridden.
    std::filesystem::path archive_dir = layered->get_string("archive_dir");
    if(archive_dir.empty()) {
        archive_dir = tasks_dir / "archive";
    }
    // Notes live next to the tasks directory (e.g. .../mdtask/notes) with their
    // own archive subfolder, unless explicitly configured.
    std::filesystem::path notes_dir = layered->get_string("notes_dir");
    if(notes_dir.empty()) {
        notes_dir = tasks_dir.parent_path() / "notes";
    }
    std::filesystem::path notes_archive_dir =
        layered->get_string("notes_archive_dir");
    if(notes_archive_dir.empty()) {
        notes_archive_dir = notes_dir / "archive";
    }

    auto categories = load_categories(*layered);
    if(!categories) {
        return std::unexpected(categories.error());
    }

    return Config{
        .log_level          = level->second,
        .tasks_dir          = tasks_dir,
        .notes_dir          = notes_dir,
        .archive_dir        = archive_dir,
        .notes_archive_dir  = notes_archive_dir,
        .log_file           = layered->get_string("log_file"),
        .date_format        = format->second,
        .language           = language->second,
        .editor             = layered->get_string("editor"),
        .categories         = std::move(*categories),
    };
}

}  // namespace mdtask
