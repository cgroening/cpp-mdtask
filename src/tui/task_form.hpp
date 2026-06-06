#pragma once

#include "config/config.hpp"
#include "domain/task.hpp"
#include "service/task_service.hpp"

#include <optional>

namespace mdtask {

/**
 * Opens an interactive form to create a new task and stores it.
 *
 * @param service Service used to persist the task.
 * @param config  Provides the editor for the multiline description field.
 * @return The created task, or std::nullopt when cancelled or invalid.
 */
[[nodiscard]] std::optional<Task> run_new_task_form(
    TaskService& service, const Config& config
);

/**
 * Opens an interactive form to edit an existing task and stores the changes.
 *
 * @param service Service used to persist the changes.
 * @param config  Provides the editor for the multiline description field.
 * @param task    The task to edit (its id, created date and project are kept).
 * @return The updated task, or std::nullopt when cancelled or invalid.
 */
[[nodiscard]] std::optional<Task> run_edit_task_form(
    TaskService& service, const Config& config, const Task& task
);

}  // namespace mdtask
