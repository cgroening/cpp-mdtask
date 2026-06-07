#include "storage/markdown_task_repository.hpp"

#include "domain/errors.hpp"
#include "storage/markdown_document.hpp"
#include "util/date.hpp"

#include <sparcli.hpp>

#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace mdtask {

namespace {

namespace fs = std::filesystem;
namespace serde = sparcli::serde;

constexpr std::size_t MAX_SLUG_LENGTH = 60;
constexpr const char* NO_DATE_PREFIX = "nodate";

/** Trims leading and trailing ASCII whitespace. */
std::string trim(std::string_view text) {
    const auto begin = text.find_first_not_of(" \t\r\n");
    if(begin == std::string_view::npos) {
        return {};
    }
    const auto end = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(begin, end - begin + 1));
}

/** Maps a priority to its serialized name. */
std::string_view priority_to_string(Priority priority) {
    switch(priority) {
        case Priority::LOW:    return "low";
        case Priority::MEDIUM: return "medium";
        case Priority::HIGH:   return "high";
        case Priority::NONE:   break;
    }
    return "none";
}

/** Parses a serialized priority name (unknown values map to NONE). */
Priority priority_from_string(std::string_view text) {
    if(text == "low")    { return Priority::LOW; }
    if(text == "medium") { return Priority::MEDIUM; }
    if(text == "high")   { return Priority::HIGH; }
    return Priority::NONE;
}

/** Serialized name for a status. */
std::string_view status_to_string(Status status) {
    switch(status) {
        case Status::IN_PROGRESS: return "in_progress";
        case Status::PAUSED:      return "paused";
        case Status::DONE:        return "done";
        case Status::CANCELLED:   return "cancelled";
        case Status::OPEN:        break;
    }
    return "open";
}

/** Parses a serialized status name (unknown values map to OPEN). */
Status status_from_string(std::string_view text) {
    if(text == "in_progress") { return Status::IN_PROGRESS; }
    if(text == "paused")      { return Status::PAUSED; }
    if(text == "done")        { return Status::DONE; }
    if(text == "cancelled")   { return Status::CANCELLED; }
    return Status::OPEN;
}

/** Reads an object member as a string, or a fallback when absent. */
std::string string_or(
    serde::View object, std::string_view key, std::string_view fallback
) {
    if(const auto value = object.get(key).as_string()) {
        return std::string(*value);
    }
    return std::string(fallback);
}

/** Reads an object member as a bool, or a fallback when absent. */
bool bool_or(serde::View object, std::string_view key, bool fallback) {
    return object.get(key).as_bool().value_or(fallback);
}

/**
 * Splits a body into its H1 title and the remaining description.
 *
 * When the first line is an H1 (`# ...`) its text is returned as the title and
 * everything after it (leading blank lines stripped) as the description.
 * Otherwise the title is empty and the whole body is the description.
 */
std::pair<std::string, std::string> split_h1(std::string_view body) {
    // The Markdown body keeps the blank line that follows the front matter;
    // strip leading newlines so the H1 is recognised as the first line.
    const auto body_start = body.find_first_not_of('\n');
    if(body_start == std::string_view::npos) {
        return {std::string{}, std::string{}};
    }
    body = body.substr(body_start);

    const auto newline = body.find('\n');
    const std::string_view first =
        newline == std::string_view::npos ? body : body.substr(0, newline);

    if(first.starts_with("# ")) {
        const std::string title = trim(first.substr(2));
        const std::string_view rest = newline == std::string_view::npos
            ? std::string_view{}
            : body.substr(newline + 1);
        return {title, trim(rest)};
    }

    return {std::string{}, trim(body)};
}

/** Builds the YAML front-matter object for a task (stable key order). */
serde::Value task_to_frontmatter(const Task& task) {
    serde::Value frontmatter = serde::Value::object();
    frontmatter.set("id", serde::Value::string(task.id));
    frontmatter.set("title", serde::Value::string(task.title));
    if(task.due) {
        frontmatter.set("due", serde::Value::string(to_iso(*task.due)));
    }
    frontmatter.set(
        "priority", serde::Value::string(priority_to_string(task.priority))
    );
    frontmatter.set("someday", serde::Value::boolean(task.someday));
    frontmatter.set(
        "status", serde::Value::string(status_to_string(task.status))
    );
    if(task.completed_at) {
        frontmatter.set(
            "completed_at", serde::Value::string(*task.completed_at)
        );
    }
    if(task.order) {
        frontmatter.set("order", serde::Value::integer(*task.order));
    }
    frontmatter.set("created", serde::Value::string(to_iso(task.created)));
    if(!task.project.empty()) {
        frontmatter.set("project", serde::Value::string(task.project));
    }
    return frontmatter;
}

/** Composes the Markdown body: an H1 title followed by the description. */
std::string compose_body(const Task& task) {
    std::string body = "# " + task.title;
    if(!task.description.empty()) {
        body += "\n\n" + task.description;
    }
    return body;
}

/** Maps a parsed Markdown document back into a Task. */
Task document_to_task(const MarkdownDocument& document, const fs::path& path) {
    const serde::View frontmatter = document.frontmatter.view();
    auto [h1_title, description] = split_h1(document.body);

    std::string title = string_or(frontmatter, "title", "");
    if(title.empty()) {
        title = !h1_title.empty() ? h1_title : path.stem().string();
    }

    Task task;
    task.id = string_or(frontmatter, "id", path.stem().string());
    task.title = title;
    task.description = description;
    task.due = parse_iso_date(string_or(frontmatter, "due", ""));
    task.priority =
        priority_from_string(string_or(frontmatter, "priority", "none"));
    task.someday = bool_or(frontmatter, "someday", false);
    if(const auto status = frontmatter.get("status").as_string()) {
        task.status = status_from_string(*status);
    } else {
        // Legacy files only have the `done` bool; map it onto the new status.
        task.status = bool_or(frontmatter, "done", false) ? Status::DONE
                                                          : Status::OPEN;
    }
    const std::string completed = string_or(frontmatter, "completed_at", "");
    if(!completed.empty()) {
        task.completed_at = completed;
    }
    if(const auto order = frontmatter.get("order").as_int()) {
        task.order = static_cast<int>(*order);
    }
    task.created =
        parse_iso_date(string_or(frontmatter, "created", "")).value_or(today());
    task.project = string_or(frontmatter, "project", "");
    return task;
}

/** Reads only a file's front-matter id, or "" when unreadable. */
std::string read_id_of(const fs::path& path) {
    try {
        if(const auto document = read_markdown(path)) {
            return string_or(document->frontmatter.view(), "id", "");
        }
    } catch(const StorageError&) {
        // Treat an unreadable neighbour as a foreign file, never our own id.
    } catch(const serde::ParseError&) {
        // Same: a malformed neighbour must not be mistaken for this task.
    }
    return {};
}

/** Year and month (zero-padded) used for an archived task's subdirectory. */
std::pair<std::string, std::string> archive_year_month(const Task& task) {
    const std::string stamp =
        task.completed_at ? *task.completed_at : to_iso(today());
    return {stamp.substr(0, 4), stamp.substr(5, 2)};
}

}  // namespace

std::string slugify(std::string_view title) {
    // First normalize to lower-case ASCII words, transliterating the German
    // letters and turning every other byte into a word separator.
    std::string normalized;
    normalized.reserve(title.size());
    for(std::size_t i = 0; i < title.size(); ++i) {
        const auto byte = static_cast<unsigned char>(title[i]);
        if(byte == 0xc3 && i + 1 < title.size()) {
            const auto next = static_cast<unsigned char>(title[i + 1]);
            const char* replacement = nullptr;
            switch(next) {
                case 0xa4: case 0x84: replacement = "ae"; break;  // ä Ä
                case 0xb6: case 0x96: replacement = "oe"; break;  // ö Ö
                case 0xbc: case 0x9c: replacement = "ue"; break;  // ü Ü
                case 0x9f:            replacement = "ss"; break;  // ß
                default:              break;
            }
            if(replacement) {
                normalized += replacement;
                ++i;
                continue;
            }
        }
        if(std::isalnum(byte) != 0) {
            normalized += static_cast<char>(std::tolower(byte));
        } else {
            normalized += ' ';
        }
    }

    // Join the words with single hyphens, capped at MAX_SLUG_LENGTH.
    std::string slug;
    bool pending_separator = false;
    for(const char character : normalized) {
        if(character == ' ') {
            pending_separator = !slug.empty();
            continue;
        }
        if(pending_separator && slug.size() < MAX_SLUG_LENGTH) {
            slug += '-';
            pending_separator = false;
        }
        if(slug.size() >= MAX_SLUG_LENGTH) {
            break;
        }
        slug += character;
    }

    return slug.empty() ? "untitled" : slug;
}

std::string task_filename(const Task& task) {
    const std::string date_part = task.due ? to_iso(*task.due) : NO_DATE_PREFIX;
    return date_part + "--" + slugify(task.title) + ".md";
}

MarkdownTaskRepository::MarkdownTaskRepository(
    std::filesystem::path tasks_dir, std::filesystem::path archive_dir
)
    : tasks_dir_(std::move(tasks_dir)),
      archive_dir_(std::move(archive_dir)) {}

std::vector<Task> MarkdownTaskRepository::load_tasks(
    const std::vector<fs::path>& paths
) {
    std::vector<Task> tasks;
    for(const auto& path : paths) {
        try {
            if(const auto document = read_markdown(path)) {
                tasks.push_back(document_to_task(*document, path));
            }
        } catch(const StorageError& error) {
            sparcli::logging::warn(
                std::string("skipping unreadable task: ") + error.what()
            );
        } catch(const serde::ParseError& error) {
            sparcli::logging::warn(
                "skipping malformed task " + path.string() + ": " + error.what()
            );
        }
    }
    return tasks;
}

std::vector<Task> MarkdownTaskRepository::find_all() const {
    // Non-recursive: archived files live under archive_dir_ and stay excluded.
    return load_tasks(list_markdown_files(tasks_dir_));
}

std::vector<Task> MarkdownTaskRepository::find_archived() const {
    // Archived files are nested under archive/<year>/<month>/, so scan deeply.
    return load_tasks(list_markdown_files_recursive(archive_dir_));
}

std::optional<Task> MarkdownTaskRepository::find_by_id(
    const std::string& id
) const {
    for(auto& task : find_all()) {
        if(task.id == id) {
            return task;
        }
    }
    return std::nullopt;
}

void MarkdownTaskRepository::save(const Task& task) {
    update(task);
}

void MarkdownTaskRepository::update(const Task& task) {
    // Find the task's current file (if any) so a changed due date or title can
    // be realized as a rename rather than a stray duplicate.
    std::optional<fs::path> existing;
    for(const auto& path : list_markdown_files(tasks_dir_)) {
        if(read_id_of(path) == task.id) {
            existing = path;
            break;
        }
    }

    // Pick a unique target name, treating a file that already belongs to this
    // task as a non-collision.
    const std::string base = task_filename(task);
    const std::string prefix = base.substr(0, base.size() - 3);  // drop ".md"
    fs::path target = tasks_dir_ / base;
    for(int suffix = 2; fs::exists(target); ++suffix) {
        if(read_id_of(target) == task.id) {
            break;
        }
        target = tasks_dir_ / (prefix + "-" + std::to_string(suffix) + ".md");
    }

    write_markdown(target, task_to_frontmatter(task), compose_body(task));

    // TODO: when projects are added (v0.2), a rename here must also update the
    // task's link inside its project file. Links use the stable front-matter
    // `id`, so this is safe today; revisit when path-based links are added.
    if(existing && *existing != target) {
        std::error_code error;
        fs::remove(*existing, error);
    }
    sparcli::logging::info("saved task " + task.id);
}

void MarkdownTaskRepository::archive(const Task& task) {
    std::optional<fs::path> existing;
    for(const auto& path : list_markdown_files(tasks_dir_)) {
        if(read_id_of(path) == task.id) {
            existing = path;
            break;
        }
    }
    if(!existing) {
        return;
    }

    const auto [year, month] = archive_year_month(task);
    const fs::path dest_dir = archive_dir_ / year / month;
    std::error_code error;
    fs::create_directories(dest_dir, error);
    if(error) {
        throw StorageError(
            "Cannot create archive directory: " + dest_dir.string()
        );
    }

    fs::path dest = dest_dir / existing->filename();
    const std::string stem = dest.stem().string();
    for(int suffix = 2; fs::exists(dest); ++suffix) {
        dest = dest_dir / (stem + "-" + std::to_string(suffix) + ".md");
    }

    fs::rename(*existing, dest, error);
    if(error) {
        // Fall back to copy+remove when the archive is on another filesystem.
        error.clear();
        fs::copy_file(*existing, dest, error);
        if(error) {
            throw StorageError("Cannot archive task: " + existing->string());
        }
        fs::remove(*existing, error);
    }
    sparcli::logging::info("archived task " + task.id);
}

void MarkdownTaskRepository::unarchive(const Task& task) {
    // Locate the archived file by id anywhere under the archive tree.
    std::optional<fs::path> source;
    for(const auto& path : list_markdown_files_recursive(archive_dir_)) {
        if(read_id_of(path) == task.id) {
            source = path;
            break;
        }
    }
    if(!source) {
        return;
    }

    // Pick a unique target name in the active directory (mirrors update()).
    const std::string base = task_filename(task);
    const std::string prefix = base.substr(0, base.size() - 3);  // drop ".md"
    fs::path target = tasks_dir_ / base;
    for(int suffix = 2; fs::exists(target); ++suffix) {
        target = tasks_dir_ / (prefix + "-" + std::to_string(suffix) + ".md");
    }

    std::error_code error;
    fs::create_directories(tasks_dir_, error);
    fs::rename(*source, target, error);
    if(error) {
        // Fall back to copy+remove when the active dir is on another filesystem.
        error.clear();
        fs::copy_file(*source, target, error);
        if(error) {
            throw StorageError("Cannot restore task: " + source->string());
        }
        fs::remove(*source, error);
    }
    sparcli::logging::info("restored task " + task.id);
}

void MarkdownTaskRepository::remove(const Task& task) {
    // The task may live in the active dir or anywhere under the archive tree.
    std::optional<fs::path> path;
    for(const auto& candidate : list_markdown_files(tasks_dir_)) {
        if(read_id_of(candidate) == task.id) {
            path = candidate;
            break;
        }
    }
    if(!path) {
        for(const auto& candidate :
            list_markdown_files_recursive(archive_dir_)) {
            if(read_id_of(candidate) == task.id) {
                path = candidate;
                break;
            }
        }
    }
    if(!path) {
        return;
    }

    std::error_code error;
    fs::remove(*path, error);
    if(error) {
        throw StorageError("Cannot delete task: " + path->string());
    }
    sparcli::logging::info("deleted task " + task.id);
}

}  // namespace mdtask
