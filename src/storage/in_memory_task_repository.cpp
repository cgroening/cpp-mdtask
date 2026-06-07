#include "storage/in_memory_task_repository.hpp"

#include <algorithm>

namespace mdtask {

std::vector<Task> InMemoryTaskRepository::find_all() const {
    return tasks_;
}

std::vector<Task> InMemoryTaskRepository::find_archived() const {
    return archived_;
}

std::optional<Task> InMemoryTaskRepository::find_by_id(
    const std::string& id
) const {
    const auto found = std::ranges::find(tasks_, id, &Task::id);
    if(found == tasks_.end()) {
        return std::nullopt;
    }
    return *found;
}

void InMemoryTaskRepository::save(const Task& task) {
    tasks_.push_back(task);
}

void InMemoryTaskRepository::update(const Task& task) {
    const auto found = std::ranges::find(tasks_, task.id, &Task::id);
    if(found != tasks_.end()) {
        *found = task;
    }
}

void InMemoryTaskRepository::archive(const Task& task) {
    // Move the task from the active set into the archive set, mirroring the
    // file-backed repository so the fake stays a valid implementation.
    if(const auto found = std::ranges::find(tasks_, task.id, &Task::id);
       found != tasks_.end()) {
        archived_.push_back(*found);
    }
    const auto removed = std::ranges::remove(tasks_, task.id, &Task::id);
    tasks_.erase(removed.begin(), removed.end());
}

void InMemoryTaskRepository::unarchive(const Task& task) {
    // Move the task from the archive set back into the active set.
    if(const auto found = std::ranges::find(archived_, task.id, &Task::id);
       found != archived_.end()) {
        tasks_.push_back(*found);
    }
    const auto removed = std::ranges::remove(archived_, task.id, &Task::id);
    archived_.erase(removed.begin(), removed.end());
}

}  // namespace mdtask
