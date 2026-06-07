#pragma once

#include "storage/task_repository.hpp"

#include <optional>
#include <string>
#include <vector>

namespace mdtask {

/**
 * In-memory TaskRepository backed by a vector.
 *
 * Keeps nothing across process restarts; its main purpose is to be a fast,
 * dependency-free fake in tests so the service can be exercised without a
 * filesystem. It is nonetheless a fully valid implementation in its own
 * right.
 */
class InMemoryTaskRepository : public TaskRepository {
public:
    [[nodiscard]] std::vector<Task> find_all() const override;
    [[nodiscard]] std::vector<Task> find_archived() const override;
    [[nodiscard]] std::optional<Task> find_by_id(
        const std::string& id
    ) const override;
    void save(const Task& task) override;
    void update(const Task& task) override;
    void archive(const Task& task) override;

private:
    std::vector<Task> tasks_;
    std::vector<Task> archived_;
};

}  // namespace mdtask
