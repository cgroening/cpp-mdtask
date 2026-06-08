#pragma once

#include "domain/task.hpp"

#include <optional>
#include <vector>

namespace mdtask {

/**
 * Picks the single task to work on next, by urgency.
 *
 * Eligible candidates are open tasks only: not terminal (done/cancelled), not
 * notes, and not deliberately deferred (`someday`). Among them the winner is
 * the one with the earliest due date (overdue first; undated tasks rank last);
 * ties break by higher priority, then the manual `order` (unset last), then
 * title.
 *
 * @param tasks The active task set (notes already excluded by the caller).
 * @return The recommended next task, or std::nullopt when none qualifies.
 */
[[nodiscard]] std::optional<Task> suggest_next_task(
    const std::vector<Task>& tasks
);

}  // namespace mdtask
