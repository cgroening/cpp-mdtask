#pragma once

#include "domain/errors.hpp"
#include "domain/task.hpp"
#include "storage/task_repository.hpp"

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace mdtask {

/** Fields supplied when creating a task (id and timestamps are derived). */
struct NewTask {
    std::string title;                                /**< Required title. */
    std::string description;                          /**< Optional body. */
    std::optional<std::chrono::year_month_day> due;   /**< Optional due date. */
    Priority priority = Priority::NONE;               /**< Priority bucket. */
    bool someday = false;                             /**< Mark as someday. */
    std::string project;                              /**< Optional project. */
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

    /** Returns all tasks (active set). */
    [[nodiscard]] std::vector<Task> all_tasks() const;

    /** Returns the tasks that are not completed yet. */
    [[nodiscard]] std::vector<Task> open_tasks() const;

    /**
     * Toggles a task between done and open, recording or clearing the
     * completion timestamp accordingly.
     *
     * @param id Id of an existing task.
     * @return The updated task, or a NOT_FOUND error.
     */
    [[nodiscard]] Result<Task> toggle_done(const std::string& id);

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
    [[nodiscard]] Result<Task> archive_task(const std::string& id);

private:
    TaskRepository& repository_;
};

}  // namespace mdtask
