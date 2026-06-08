#pragma once

#include "storage/task_repository.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace mdtask {

/**
 * TaskRepository that stores one Markdown file per task.
 *
 * Each file lives in the tasks directory, named `<due>--<slug>.md` (or
 * `nodate--<slug>.md` when the task has no due date), with metadata in YAML
 * front matter and the title/description in the body. The file name always
 * uses ISO `YYYY-MM-DD` for the date part so files sort chronologically
 * regardless of the user's display format. The stable `id` in the front
 * matter - not the file name - is the source of truth for identity.
 *
 * Archiving moves a file into `archive/<year>/<month>/` instead of deleting
 * it, so no data is lost.
 */
class MarkdownTaskRepository : public TaskRepository {
public:
    /**
     * @param tasks_dir          Directory holding the active task files.
     * @param notes_dir          Directory holding the active note files.
     * @param archive_dir        Root directory for archived task files.
     * @param notes_archive_dir  Root directory for archived note files.
     */
    MarkdownTaskRepository(
        std::filesystem::path tasks_dir,
        std::filesystem::path notes_dir,
        std::filesystem::path archive_dir,
        std::filesystem::path notes_archive_dir
    );

    [[nodiscard]] std::vector<Task> find_all() const override;
    [[nodiscard]] std::vector<Task> find_notes() const override;
    [[nodiscard]] std::vector<Task> find_archived() const override;
    [[nodiscard]] std::optional<Task> find_by_id(
        const std::string& id
    ) const override;
    [[nodiscard]] std::vector<std::string> load_warnings() const override;
    void save(const Task& task) override;
    void update(const Task& task) override;
    void archive(const Task& task) override;
    void unarchive(const Task& task) override;
    void remove(const Task& task) override;

private:
    /**
     * Reads and parses the items at `paths`, stamping each with `note`. A file
     * that cannot be read or parsed is skipped and recorded in `load_warnings_`
     * instead of aborting the whole load.
     */
    [[nodiscard]] std::vector<Task> load_tasks(
        const std::vector<std::filesystem::path>& paths, bool note
    ) const;

    /** Active directory an item belongs in, by its note flag. */
    [[nodiscard]] const std::filesystem::path& active_dir(bool note) const;
    /** Archive root an item belongs in, by its note flag. */
    [[nodiscard]] const std::filesystem::path& archive_root(bool note) const;

    std::filesystem::path tasks_dir_;
    std::filesystem::path notes_dir_;
    std::filesystem::path archive_dir_;
    std::filesystem::path notes_archive_dir_;

    /** Human-readable warnings from the most recent load (skipped files). */
    mutable std::vector<std::string> load_warnings_;
};

/**
 * Builds a filesystem-safe slug from a title.
 *
 * Lower-cased; German umlauts and sharp s are transliterated (ä→ae, ß→ss);
 * every other run of non-alphanumeric characters becomes a single hyphen;
 * leading/trailing hyphens are trimmed and the result is length-limited. An
 * empty result falls back to "untitled". Exposed for unit testing.
 */
[[nodiscard]] std::string slugify(std::string_view title);

/**
 * Returns the base file name for a task: `<due-iso>--<slug>.md`, or
 * `nodate--<slug>.md` when the task has no due date. Exposed for unit testing;
 * collision suffixes are added by the repository, not here.
 */
[[nodiscard]] std::string task_filename(const Task& task);

}  // namespace mdtask
