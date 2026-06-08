#include "tui/finder_actions.hpp"

namespace mdtask {

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
