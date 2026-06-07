#include "service/task_service.hpp"

#include "util/date.hpp"

#include <sparcli.hpp>

#include <algorithm>
#include <cctype>
#include <random>
#include <ranges>
#include <string>

namespace mdtask {

namespace {

constexpr int ID_LENGTH = 12;

/** Trims leading and trailing ASCII whitespace from a copy of the input. */
std::string trim(const std::string& text) {
    const auto is_space = [](unsigned char character) {
        return std::isspace(character) != 0;
    };
    const auto begin = std::ranges::find_if_not(text, is_space);
    const auto end = std::ranges::find_if_not(
        text | std::views::reverse, is_space
    ).base();
    if(begin >= end) {
        return {};
    }
    return std::string(begin, end);
}

/** Generates a short, random, lowercase-hex id. */
std::string generate_id() {
    static thread_local std::mt19937 generator{std::random_device{}()};
    std::uniform_int_distribution<int> hex_digit(0, 15);

    std::string id;
    id.reserve(ID_LENGTH);
    for(int i = 0; i < ID_LENGTH; ++i) {
        id.push_back("0123456789abcdef"[hex_digit(generator)]);
    }
    return id;
}

/**
 * Aligns `completed_at` with the task's status: a DONE task gets a timestamp
 * (kept if it already has one), any other status clears it.
 */
void reconcile_completed_at(Task& task) {
    if(task.status == Status::DONE) {
        if(!task.completed_at) {
            task.completed_at = now_timestamp();
        }
    } else {
        task.completed_at.reset();
    }
}

}  // namespace

TaskService::TaskService(TaskRepository& repository)
    : repository_(repository) {}

Result<Task> TaskService::add_task(const NewTask& fields) {
    const std::string clean_title = trim(fields.title);
    if(clean_title.empty()) {
        return std::unexpected(
            validation_error("a task title must not be empty")
        );
    }

    const Task task{
        .id          = generate_id(),
        .title       = clean_title,
        .description = fields.description,
        .due         = fields.due,
        .priority    = fields.priority,
        .someday     = fields.someday,
        .status      = Status::OPEN,
        .created     = today(),
        .project     = fields.project,
    };
    repository_.save(task);
    sparcli::logging::info("added task " + task.id);
    return task;
}

Result<Task> TaskService::update_task(Task task) {
    const std::string clean_title = trim(task.title);
    if(clean_title.empty()) {
        return std::unexpected(
            validation_error("a task title must not be empty")
        );
    }
    task.title = clean_title;
    reconcile_completed_at(task);
    repository_.update(task);
    return task;
}

std::vector<Task> TaskService::all_tasks() const {
    return repository_.find_all();
}

std::vector<Task> TaskService::archived_tasks() const {
    return repository_.find_archived();
}

std::vector<Task> TaskService::open_tasks() const {
    auto tasks = repository_.find_all();
    const auto removed = std::ranges::remove_if(tasks, [](const Task& task) {
        return task.status == Status::DONE;
    });
    tasks.erase(removed.begin(), removed.end());
    return tasks;
}

Result<Task> TaskService::set_status(const std::string& id, Status status) {
    auto task = repository_.find_by_id(id);
    if(!task) {
        return std::unexpected(task_not_found_error(id));
    }

    task->status = status;
    reconcile_completed_at(*task);
    repository_.update(*task);
    sparcli::logging::info(
        "task " + id + " status set to "
            + std::to_string(static_cast<int>(status))
    );
    return *task;
}

Result<Task> TaskService::toggle_done(const std::string& id) {
    const auto task = repository_.find_by_id(id);
    if(!task) {
        return std::unexpected(task_not_found_error(id));
    }
    // Toggle between DONE and OPEN; in-progress counts as not-done.
    return set_status(
        id, task->status == Status::DONE ? Status::OPEN : Status::DONE
    );
}

Result<Task> TaskService::shift_due(const std::string& id, int days) {
    auto task = repository_.find_by_id(id);
    if(!task) {
        return std::unexpected(task_not_found_error(id));
    }

    // A dateless task lands on today on the first +/- press; once it has a
    // date, further presses shift it by the given number of days.
    task->due = task->due ? shift_days(*task->due, days) : today();
    // Moving an Inbox task onto the calendar drops its someday flag.
    task->someday = false;
    repository_.update(*task);
    return *task;
}

Result<Task> TaskService::set_priority(
    const std::string& id, Priority priority
) {
    auto task = repository_.find_by_id(id);
    if(!task) {
        return std::unexpected(task_not_found_error(id));
    }
    task->priority = priority;
    repository_.update(*task);
    return *task;
}

Result<Task> TaskService::archive_task(const std::string& id) {
    auto task = repository_.find_by_id(id);
    if(!task) {
        return std::unexpected(task_not_found_error(id));
    }
    repository_.archive(*task);
    sparcli::logging::info("archived task " + id);
    return *task;
}

Result<Task> TaskService::restore_task(const std::string& id) {
    // The active lookup does not see archived tasks, so search the archive.
    for(const auto& task : repository_.find_archived()) {
        if(task.id == id) {
            repository_.unarchive(task);
            sparcli::logging::info("restored task " + id);
            return task;
        }
    }
    return std::unexpected(task_not_found_error(id));
}

}  // namespace mdtask
