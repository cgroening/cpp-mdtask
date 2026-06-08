#include "domain/suggestion.hpp"

namespace mdtask {

namespace {

/** True for a task eligible to be recommended as the next one to work on. */
bool is_candidate(const Task& task) {
    return !is_terminal(task.status) && !task.note && !task.someday;
}

/**
 * Orders two candidates by urgency: earlier due first (undated last), then
 * higher priority, then the manual order (unset last), then title.
 */
bool more_urgent(const Task& a, const Task& b) {
    if(a.due != b.due) {
        if(!a.due) { return false; }   // undated sorts after dated
        if(!b.due) { return true; }
        return *a.due < *b.due;        // earlier due date is more urgent
    }
    if(a.priority != b.priority) {
        return static_cast<int>(a.priority) > static_cast<int>(b.priority);
    }
    if(a.order != b.order) {
        if(!a.order) { return false; }   // unset order sorts to the bottom
        if(!b.order) { return true; }
        return *a.order < *b.order;
    }
    return a.title < b.title;
}

}  // namespace

std::optional<Task> suggest_next_task(const std::vector<Task>& tasks) {
    const Task* best = nullptr;
    for(const auto& task : tasks) {
        if(!is_candidate(task)) {
            continue;
        }
        if(!best || more_urgent(task, *best)) {
            best = &task;
        }
    }
    if(!best) {
        return std::nullopt;
    }
    return *best;
}

}  // namespace mdtask
