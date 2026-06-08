#include "service/task_service.hpp"

#include "domain/agenda.hpp"
#include "domain/duplicate.hpp"
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
    if(is_terminal(task.status)) {
        if(!task.completed_at) {
            task.completed_at = now_timestamp();
        }
    } else {
        task.completed_at.reset();
    }
}

/** Strips the task-only fields a note must not carry (kept: title, body). */
void normalize_note(Task& task) {
    task.due.reset();
    task.priority = Priority::NONE;
    task.someday = false;
    task.status = Status::OPEN;
    task.completed_at.reset();
    task.project.clear();
}

}  // namespace

TaskService::TaskService(TaskRepository& repository)
    : repository_(repository) {}

/** One past the largest order among the matching active list, so an item lands
    last in its own list (tasks and notes order independently). */
int TaskService::append_order(bool note) const {
    int max = -1;
    const auto items = note ? repository_.find_notes() : repository_.find_all();
    for(const auto& item : items) {
        if(!is_terminal(item.status) && item.order) {
            max = std::max(max, *item.order);
        }
    }
    return max + 1;
}

Result<Task> TaskService::add_task(const NewTask& fields) {
    const std::string clean_title = trim(fields.title);
    if(clean_title.empty()) {
        return std::unexpected(
            validation_error("a task title must not be empty")
        );
    }

    Task task{
        .id          = generate_id(),
        .title       = clean_title,
        .description = fields.description,
        .due         = fields.due,
        .priority    = fields.priority,
        .someday     = fields.someday,
        .status      = Status::OPEN,
        .order       = append_order(fields.note),   // last in its own list
        .note        = fields.note,
        .created     = today(),
        .project     = fields.project,
    };
    if(task.note) {
        normalize_note(task);
    }
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

    const auto stored = repository_.find_by_id(task.id);
    if(task.note) {
        normalize_note(task);   // a note sheds the task-only fields
    } else {
        reconcile_completed_at(task);
    }

    // Re-append at the end of the target list when the item moved lists (note
    // toggled) or, for a task, changed its day.
    const bool note_changed = stored && stored->note != task.note;
    const bool due_changed = stored && stored->due != task.due;
    if(note_changed || due_changed) {
        task.order = append_order(task.note);
    }
    repository_.update(task);
    return task;
}

Result<Task> TaskService::duplicate_task(const std::string& id) {
    const auto source = repository_.find_by_id(id);
    if(!source) {
        return std::unexpected(task_not_found_error(id));
    }

    // Unique, numbered title among the same kind of item (tasks vs notes).
    std::vector<std::string> titles;
    const auto siblings =
        source->note ? repository_.find_notes() : repository_.find_all();
    for(const auto& item : siblings) {
        titles.push_back(item.title);
    }

    // add_task assigns a fresh id, OPEN status, a trailing order and today's
    // creation date, and normalizes a note - so only the content carries over.
    return add_task({
        .title       = next_copy_title(source->title, titles),
        .description = source->description,
        .due         = source->due,
        .priority    = source->priority,
        .someday     = source->someday,
        .project     = source->project,
        .note        = source->note,
    });
}

std::vector<Task> TaskService::all_tasks() const {
    return repository_.find_all();
}

std::vector<Task> TaskService::notes() const {
    return repository_.find_notes();
}

std::vector<Task> TaskService::archived_tasks() const {
    return repository_.find_archived();
}

std::vector<std::string> TaskService::load_warnings() const {
    return repository_.load_warnings();
}

std::optional<std::filesystem::path> TaskService::file_path(
    const std::string& id
) const {
    return repository_.file_path(id);
}

Result<Task> TaskService::reload_task(const std::string& id) {
    // find_by_id reads fresh from disk and skips malformed files, so a broken
    // edit (or a removed id) surfaces here as NOT_FOUND and is left untouched.
    auto task = repository_.find_by_id(id);
    if(!task) {
        return std::unexpected(task_not_found_error(id));
    }
    // Re-save through the normal path so a changed due date or title renames the
    // file to keep the <due>--<slug>.md invariant.
    return update_task(std::move(*task));
}

std::vector<Task> TaskService::open_tasks() const {
    auto tasks = repository_.find_all();
    const auto removed = std::ranges::remove_if(tasks, [](const Task& task) {
        return is_terminal(task.status);
    });
    tasks.erase(removed.begin(), removed.end());
    return tasks;
}

Result<Task> TaskService::set_status(const std::string& id, Status status) {
    auto task = repository_.find_by_id(id);
    if(!task) {
        return std::unexpected(task_not_found_error(id));
    }

    // Reopening a terminal task sends it to the end of the active tasks.
    if(is_terminal(task->status) && !is_terminal(status)) {
        task->order = append_order(task->note);
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
    // The day changed, so re-append it under the other tasks of the new date.
    task->order = append_order(task->note);
    repository_.update(*task);
    return *task;
}

Result<Task> TaskService::set_due(
    const std::string& id, std::optional<std::chrono::year_month_day> due
) {
    auto task = repository_.find_by_id(id);
    if(!task) {
        return std::unexpected(task_not_found_error(id));
    }
    task->due = due;
    if(due) {
        // A dated task is no longer "someday"; re-append it under its new day.
        task->someday = false;
        task->order = append_order(task->note);
    }
    repository_.update(*task);
    return *task;
}

Result<Task> TaskService::move_task(const std::string& id, MoveDir direction) {
    const auto target = repository_.find_by_id(id);
    if(!target) {
        return std::unexpected(task_not_found_error(id));
    }
    if(is_terminal(target->status)) {
        return *target;   // terminal tasks keep their completion order
    }

    // Collect the active items the target is displayed among: the whole notes
    // list for a note, otherwise the agenda section the task sits in.
    std::vector<Task> active;
    if(target->note) {
        for(const auto& note : repository_.find_notes()) {
            if(!is_terminal(note.status)) {
                active.push_back(note);
            }
        }
        std::ranges::sort(active, [](const Task& a, const Task& b) {
            if(a.order != b.order) {
                if(!a.order) { return false; }
                if(!b.order) { return true; }
                return *a.order < *b.order;
            }
            return a.title < b.title;
        });
    } else {
        const auto agenda = build_agenda(repository_.find_all(), today());
        for(const auto& section : agenda) {
            const bool here = std::ranges::any_of(
                section.tasks, [&](const Task& t) { return t.id == id; }
            );
            if(here) {
                for(const auto& candidate : section.tasks) {
                    if(!is_terminal(candidate.status)) {
                        active.push_back(candidate);
                    }
                }
                break;
            }
        }
    }

    const auto found = std::ranges::find(active, id, &Task::id);
    if(found == active.end()) {
        return *target;
    }
    const std::size_t i = static_cast<std::size_t>(found - active.begin());
    std::size_t j = i;
    switch(direction) {
        case MoveDir::UP:     if(i > 0) { j = i - 1; } break;
        case MoveDir::DOWN:   if(i + 1 < active.size()) { j = i + 1; } break;
        case MoveDir::TOP:    j = 0; break;
        case MoveDir::BOTTOM: j = active.size() - 1; break;
    }
    if(j == i) {
        return *target;   // already at the edge
    }

    const Task moved = active[i];
    active.erase(active.begin() + static_cast<std::ptrdiff_t>(i));
    active.insert(active.begin() + static_cast<std::ptrdiff_t>(j), moved);

    // Renumber the section's active tasks to 0..n-1, persisting what changed.
    for(std::size_t k = 0; k < active.size(); ++k) {
        const int want = static_cast<int>(k);
        if(active[k].order != want) {
            active[k].order = want;
            repository_.update(active[k]);
        }
    }
    return active[j];
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

Result<Task> TaskService::delete_task(const std::string& id) {
    // Look in the active set first, then the archive.
    auto task = repository_.find_by_id(id);
    if(!task) {
        for(const auto& archived : repository_.find_archived()) {
            if(archived.id == id) {
                task = archived;
                break;
            }
        }
    }
    if(!task) {
        return std::unexpected(task_not_found_error(id));
    }
    repository_.remove(*task);
    sparcli::logging::info("deleted task " + id);
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
