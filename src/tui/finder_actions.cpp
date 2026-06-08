#include "tui/finder_actions.hpp"

namespace mdtask {

std::vector<HelpItem> help_entries() {
    const auto section = [](std::string title) {
        return HelpItem{.section = std::move(title)};
    };
    const auto row = [](std::string key, std::string desc) {
        return HelpItem{.key = std::move(key), .desc = std::move(desc)};
    };
    return {
        section("Navigation"),
        row("up/down or j/k", "move cursor"),
        row("i", "filter (type to search); Esc back to normal"),
        row("s", "jump to a section"),
        row("1 / 2 / 3 / 4 / 5",
            "switch view (Tasks / Recurring / Notes / Archive / Search)"),
        row("Enter", "edit the item in a form"),
        row("e", "open the whole .md file in $EDITOR"),
        row("q or Esc", "quit"),

        section("Actions (Tasks / Notes)"),
        row("r", "toggle the next-task suggestion (Tasks only)"),
        row("R", "jump to the suggested next task"),
        row("d", "toggle done"),
        row("p", "cycle status (open / in progress / paused / cancelled)"),
        row("t", "pick a due date (calendar)"),
        row("+ / -", "shift the due date by one day"),
        row("a", "archive"),
        row("c", "duplicate the item (adds a numbered (copy) suffix)"),
        row("Delete", "delete permanently"),
        row("n / N", "new item / new opposite type"),
        row("Alt+up/down", "reorder (Alt+Shift = to top / bottom)"),

        section("Multi-select"),
        row("Space", "mark / unmark the item"),
        row("d p a t + - Del", "apply to every marked item"),

        section("Archive"),
        row("r", "restore the item"),

        section("Other"),
        row("?", "show this help"),
    };
}

Status next_status(Status status) {
    switch(status) {
        case Status::OPEN:        return Status::IN_PROGRESS;
        case Status::IN_PROGRESS: return Status::PAUSED;
        case Status::PAUSED:      return Status::CANCELLED;
        case Status::CANCELLED:   break;
        case Status::DONE:        break;
    }
    return Status::OPEN;
}

bool action_is_bulk(int action) {
    switch(action) {
        case ACT_TOGGLE_DONE:
        case ACT_CYCLE_STATUS:
        case ACT_ARCHIVE:
        case ACT_DELETE:
        case ACT_SHIFT_PLUS:
        case ACT_SHIFT_MINUS:
        case ACT_PICK_DATE:
            return true;
        default:
            return false;
    }
}

std::vector<std::string> action_targets(
    int action, const std::string& cursor_id,
    const std::vector<std::string>& selection
) {
    if(!selection.empty() && action_is_bulk(action)) {
        return selection;
    }
    return {cursor_id};
}

std::optional<std::string> focus_after_delete(
    std::span<const std::optional<std::string>> row_task_ids, std::size_t cursor
) {
    const std::size_t count = row_task_ids.size();

    // Tasks of one section are contiguous; a neighbour task means same section.
    if(cursor + 1 < count && row_task_ids[cursor + 1]) {
        return row_task_ids[cursor + 1];
    }
    if(cursor > 0 && row_task_ids[cursor - 1]) {
        return row_task_ids[cursor - 1];
    }
    // The section is now empty: first task of the next section...
    for(std::size_t i = cursor + 1; i < count; ++i) {
        if(row_task_ids[i]) {
            return row_task_ids[i];
        }
    }
    // ...otherwise the nearest task in a previous section.
    for(std::size_t i = cursor; i-- > 0;) {
        if(row_task_ids[i]) {
            return row_task_ids[i];
        }
    }
    return std::nullopt;
}

}  // namespace mdtask
