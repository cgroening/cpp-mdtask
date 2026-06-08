#pragma once

#include <string_view>

namespace mdtask {

/** Tally of checkbox subtasks found in a task body. */
struct SubtaskProgress {
    int done = 0;    /**< Ticked boxes (`- [x]`). */
    int total = 0;   /**< All boxes (ticked plus open). */
};

/**
 * Counts GitHub-style checkbox subtasks in a Markdown body.
 *
 * A subtask is a list item whose marker is immediately a checkbox: optional
 * leading whitespace, a bullet (`-`, `*` or `+`), a single space, then `[ ]`
 * (open) or `[x]`/`[X]` (done), followed by end of line or whitespace. Anything
 * else on the line is ignored.
 *
 * @param body Free-form Markdown (the task description).
 * @return The done/total tally; `{0, 0}` when the body holds no checkboxes.
 */
[[nodiscard]] SubtaskProgress count_subtasks(std::string_view body);

}  // namespace mdtask
