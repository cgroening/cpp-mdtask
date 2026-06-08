#pragma once

#include "domain/task.hpp"

#include <optional>
#include <string>
#include <vector>

namespace mdtask {

/**
 * Storage interface (the "port") that hides how tasks are persisted.
 *
 * The service layer depends only on this abstraction, never on a concrete
 * implementation (Dependency Inversion). Swapping the backing store - file,
 * in-memory, a future database - therefore touches only the composition
 * root in main.cpp.
 */
class TaskRepository {
public:
    virtual ~TaskRepository() = default;

    /**
     * Returns all stored tasks.
     *
     * @return All tasks in storage order; empty vector when none exist.
     */
    [[nodiscard]] virtual std::vector<Task> find_all() const = 0;

    /**
     * Returns all active notes (the quick-note items).
     *
     * @return Notes; empty vector when none exist.
     */
    [[nodiscard]] virtual std::vector<Task> find_notes() const = 0;

    /**
     * Returns all archived items (tasks and notes), each flagged via `note`.
     *
     * @return Archived items; empty vector when none have been archived.
     */
    [[nodiscard]] virtual std::vector<Task> find_archived() const = 0;

    /**
     * Looks up a single task.
     *
     * @param id Id of the task to find.
     * @return The task, or std::nullopt when no task has that id.
     */
    [[nodiscard]] virtual std::optional<Task> find_by_id(
        const std::string& id
    ) const = 0;

    /**
     * Warnings from the most recent load (e.g. files skipped because their
     * front matter could not be parsed). A backend that cannot fail to load
     * returns an empty vector.
     *
     * @return Human-readable warning lines; empty when the last load was clean.
     */
    [[nodiscard]] virtual std::vector<std::string> load_warnings() const = 0;

    /**
     * Persists a new task.
     *
     * @param task The task to store.
     * @throws StorageError when the task cannot be written.
     */
    virtual void save(const Task& task) = 0;

    /**
     * Overwrites an existing task, matched by its id.
     *
     * @param task The task to update; unknown ids are ignored.
     * @throws StorageError when the change cannot be written.
     */
    virtual void update(const Task& task) = 0;

    /**
     * Archives a task: removes it from the active set, retaining the data in
     * an archive location instead of deleting it.
     *
     * @param task The task to archive; unknown ids are ignored.
     * @throws StorageError when the task cannot be moved.
     */
    virtual void archive(const Task& task) = 0;

    /**
     * Restores an archived task back into the active set (the inverse of
     * archive).
     *
     * @param task The task to restore; unknown ids are ignored.
     * @throws StorageError when the task cannot be moved.
     */
    virtual void unarchive(const Task& task) = 0;

    /**
     * Permanently removes a task, whether active or archived.
     *
     * @param task The task to delete; unknown ids are ignored.
     * @throws StorageError when the task cannot be removed.
     */
    virtual void remove(const Task& task) = 0;
};

}  // namespace mdtask
