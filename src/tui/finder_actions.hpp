#pragma once

#include "domain/task.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mdtask {

/**
 * Shortcut action ids reported by the finder's `Shortcuts::fired()`.
 *
 * Shared between `run_task_finder` (which binds keys to them) and the pure
 * action logic below (which decides what each id does), so the two never drift.
 * A bare Enter reports `ACT_NONE`.
 */
enum FinderAction {
    ACT_NONE           = 0,
    ACT_TOGGLE_DONE    = 1,
    ACT_SHIFT_PLUS     = 2,
    ACT_SHIFT_MINUS    = 3,
    ACT_ARCHIVE        = 4,
    ACT_NEW            = 5,
    ACT_RESTORE        = 7,
    ACT_JUMP           = 8,
    ACT_CYCLE_STATUS   = 9,
    ACT_MOVE_UP        = 10,
    ACT_MOVE_DOWN      = 11,
    ACT_MOVE_TOP       = 12,
    ACT_MOVE_BOTTOM    = 13,
    ACT_DELETE         = 15,
    ACT_QUIT           = 16,
    ACT_NEW_OTHER      = 17,
    ACT_TOGGLE_SELECT  = 18,
    ACT_PICK_DATE      = 19,
    ACT_HELP           = 20,
    ACT_TOGGLE_SUGGEST = 21,
    ACT_FOCUS_SUGGEST  = 22,
    ACT_VIEW_TASKS     = 23,
    ACT_VIEW_RECURRING = 24,
    ACT_VIEW_NOTES     = 25,
    ACT_VIEW_ARCHIVE   = 26,
    ACT_VIEW_SEARCH    = 27,
    ACT_EDIT_FILE      = 28,
    ACT_DUPLICATE      = 29,
};

/**
 * Progress toggle for `p`: in progress <-> paused. Any other status (open,
 * done, cancelled) starts at in progress, so a second press then pauses it.
 */
[[nodiscard]] Status toggle_progress(Status status);

/**
 * Done/cancelled cycle for `d`: done -> cancelled -> open. Any other status
 * (open, in progress, paused) jumps to done, so `d` first completes a task and
 * further presses cycle done -> cancelled -> open.
 */
[[nodiscard]] Status cycle_done(Status status);

/**
 * True when `action` applies to every selected task at once (done, status
 * cycle, archive, delete, date shift, date pick). Reorder is deliberately
 * cursor-only and excluded.
 */
[[nodiscard]] bool action_is_bulk(int action);

/**
 * The task ids an action operates on: the whole `selection` when it is
 * non-empty and the action is bulk-capable, otherwise just `cursor_id`.
 *
 * @param action    The fired action id.
 * @param cursor_id Id of the task under the cursor.
 * @param selection Ids the user marked with Space (order preserved).
 * @return The target ids; never empty (falls back to the cursor task).
 */
[[nodiscard]] std::vector<std::string> action_targets(
    int action, const std::string& cursor_id,
    const std::vector<std::string>& selection
);

/**
 * Picks which task to focus after deleting the row at `cursor`, so the cursor
 * stays put instead of jumping to the top: the row that slides up into the gap
 * (next task in the same section), else the task above, else the first task of
 * the next section, else the nearest task of a previous section.
 *
 * @param row_task_ids Per-row task id, or nullopt for a section-header row.
 * @param cursor       Index of the row being deleted.
 * @return The id to focus, or nullopt when no task is left.
 */
[[nodiscard]] std::optional<std::string> focus_after_delete(
    std::span<const std::optional<std::string>> row_task_ids, std::size_t cursor
);

}  // namespace mdtask
