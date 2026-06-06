#include "storage/in_memory_task_repository.hpp"

#include <algorithm>

namespace mdtask {

std::vector<Task> InMemoryTaskRepository::find_all() const {
    return tasks_;
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
    // Archiving simply removes the task from the active in-memory set; there
    // is no separate store to keep it in for this fake.
    const auto removed = std::ranges::remove(tasks_, task.id, &Task::id);
    tasks_.erase(removed.begin(), removed.end());
}

}  // namespace mdtask
