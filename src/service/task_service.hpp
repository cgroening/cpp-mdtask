#pragma once

#include "domain/errors.hpp"
#include "domain/recurrence.hpp"
#include "domain/task.hpp"
#include "storage/task_repository.hpp"

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace mdtask {

/** Direction for moving a task within its finder section. */
enum class MoveDir { UP, DOWN, TOP, BOTTOM };

/** Fields supplied when creating a task (id and timestamps are derived). */
struct NewTask {
    std::string title;                                /**< Required title. */
    std::string description;                          /**< Optional body. */
    std::optional<std::chrono::year_month_day> due;   /**< Optional due date. */
    Priority priority = Priority::NONE;               /**< Priority bucket. */
    bool someday = false;                             /**< Mark as someday. */
    std::string project;                              /**< Optional project. */
    std::string category;                             /**< Optional category. */
    bool note = false;                                /**< Create as a note. */
    std::optional<RecurrenceRule> recurrence;         /**< Optional recurrence. */
};

/**
 * Business logic for managing tasks (the application's service layer).
 *
 * Owns every rule that is neither pure presentation nor pure storage: input
 * validation, id and timestamp generation, and the state transitions
 * (done/open, due shifting, priority). It reaches persistence only through the
 * TaskRepository interface, so it can be unit tested against a fake with no
 * filesystem involved.
 *
 * Expected failures (bad input, unknown id) are returned as Result errors;
 * only unrecoverable storage failures escape as StorageError exceptions.
 */
class TaskService {
public:
    /**
     * Creates the service on top of a repository.
     *
     * @param repository Storage backend; only the interface is known here
     *                   (Dependency Inversion).
     */
    explicit TaskService(TaskRepository& repository);

    /**
     * Creates and stores a task.
     *
     * @param fields New task fields; the title is trimmed and must not be empty.
     * @return The created task including its generated id, or a
     *         VALIDATION_FAILED error when the trimmed title is empty.
     */
    [[nodiscard]] Result<Task> add_task(const NewTask& fields);

    /**
     * Persists edits to an existing task.
     *
     * @param task The task to store; its title is trimmed and must not be empty.
     * @return The stored task, or a VALIDATION_FAILED error for an empty title.
     */
    [[nodiscard]] Result<Task> update_task(Task task);

    /**
     * Creates a copy of an existing task or note: the content fields are copied,
     * the title gets a numbered " (copy)" suffix, and a fresh id, OPEN status,
     * order and creation date are assigned. A note duplicates as a note.
     *
     * @param id Id of the task/note to duplicate.
     * @return The created duplicate, or a NOT_FOUND error.
     */
    [[nodiscard]] Result<Task> duplicate_task(const std::string& id);

    /** Returns all tasks (active set, notes excluded). */
    [[nodiscard]] std::vector<Task> all_tasks() const;

    /** Returns all active notes. */
    [[nodiscard]] std::vector<Task> notes() const;

    /** Returns the archived tasks (moved out of the active set). */
    [[nodiscard]] std::vector<Task> archived_tasks() const;

    /** Returns the tasks that are not completed yet. */
    [[nodiscard]] std::vector<Task> open_tasks() const;

    /** Warnings from the most recent load (files skipped as malformed). */
    [[nodiscard]] std::vector<std::string> load_warnings() const;

    /** Path of the file backing a task/note, or std::nullopt when unknown. */
    [[nodiscard]] std::optional<std::filesystem::path> file_path(
        const std::string& id
    ) const;

    /**
     * Re-reads a task from disk after an external edit and saves it again so the
     * file name stays in sync with the (possibly changed) due date and title.
     *
     * @param id Id of the edited task.
     * @return The reloaded, re-saved task, or a NOT_FOUND error when the file
     *         can no longer be parsed or its id changed (it is then left as is).
     */
    [[nodiscard]] Result<Task> reload_task(const std::string& id);

    /**
     * Toggles a task between done and open, recording or clearing the
     * completion timestamp accordingly.
     *
     * @param id Id of an existing task.
     * @return The updated task, or a NOT_FOUND error.
     */
    [[nodiscard]] Result<Task> toggle_done(const std::string& id);

    /**
     * Sets a task's lifecycle status, recording or clearing the completion
     * timestamp to match (DONE records it; any other status clears it).
     *
     * @param id     Id of an existing task.
     * @param status The new status.
     * @return The updated task, or a NOT_FOUND error.
     */
    [[nodiscard]] Result<Task> set_status(
        const std::string& id, Status status
    );

    /**
     * Shifts a task's due date by `days`. A task without a due date receives
     * one relative to today, so shifting can pull an Inbox task onto the
     * calendar.
     *
     * @param id   Id of an existing task.
     * @param days Offset in days (negative moves earlier).
     * @return The updated task, or a NOT_FOUND error.
     */
    [[nodiscard]] Result<Task> shift_due(const std::string& id, int days);

    /**
     * Sets a task's due date to a specific day, or clears it.
     *
     * Setting a date drops the `someday` flag (the task joins the calendar);
     * clearing it leaves the flag untouched. The task is re-appended at the end
     * of its new day so it sorts beneath the others there.
     *
     * @param id  Id of an existing task.
     * @param due The new due date, or std::nullopt to clear it.
     * @return The updated task, or a NOT_FOUND error.
     */
    [[nodiscard]] Result<Task> set_due(
        const std::string& id, std::optional<std::chrono::year_month_day> due
    );

    /**
     * Sets a task's priority.
     *
     * @param id       Id of an existing task.
     * @param priority New priority.
     * @return The updated task, or a NOT_FOUND error.
     */
    [[nodiscard]] Result<Task> set_priority(
        const std::string& id, Priority priority
    );

    /**
     * Archives a task (moves it out of the active set).
     *
     * @param id Id of an existing task.
     * @return The archived task, or a NOT_FOUND error.
     */
    /**
     * Moves a task within its finder section by adjusting the manual order.
     *
     * @param id        Id of an active task (done tasks are not reorderable).
     * @param direction Up/down by one, or all the way to the top/bottom.
     * @return The moved task, or a NOT_FOUND error.
     */
    [[nodiscard]] Result<Task> move_task(
        const std::string& id, MoveDir direction
    );

    [[nodiscard]] Result<Task> archive_task(const std::string& id);

    /**
     * Restores an archived task back into the active set.
     *
     * @param id Id of an archived task.
     * @return The restored task, or a NOT_FOUND error when no archived task
     *         has that id.
     */
    [[nodiscard]] Result<Task> restore_task(const std::string& id);

    /**
     * Permanently deletes a task (active or archived).
     *
     * @param id Id of the task to delete.
     * @return The deleted task, or a NOT_FOUND error.
     */
    [[nodiscard]] Result<Task> delete_task(const std::string& id);

private:
    /** One past the largest order among the matching active list (tasks or
        notes), so an item lands last in its own list. */
    [[nodiscard]] int append_order(bool note) const;

    TaskRepository& repository_;
};

}  // namespace mdtask
