#pragma once

#include "config/config.hpp"
#include "service/task_service.hpp"

namespace mdtask {

/**
 * Runs the interactive fuzzy agenda - the application's main screen.
 *
 * Tasks are grouped into day sections (OVERDUE, Inbox, the dated days, Without
 * date) with priority colors and a per-task status. Keys (in normal mode):
 * Enter edits, `d` toggles done, `+`/`-` shift the due date by a day, `a`
 * archives, `n` adds a task, `i` filters, Esc quits. After every change the
 * agenda is rebuilt and the cursor is restored to the same task.
 *
 * @param service Service providing and mutating the tasks.
 * @param config  Display configuration (date format, editor).
 */
void run_task_finder(TaskService& service, const Config& config);

}  // namespace mdtask
