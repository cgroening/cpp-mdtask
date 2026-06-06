#include "domain/agenda.hpp"

#include <algorithm>
#include <map>
#include <tuple>
#include <utility>

namespace mdtask {

namespace {

/** Sort key that orders tasks within any section consistently. */
auto order_key(const Task& task) {
    // Open tasks first, then earliest due, then highest priority, then title.
    // A missing due sorts last; priority is negated so HIGH (largest enum)
    // comes first.
    const std::chrono::sys_days due =
        task.due ? std::chrono::sys_days{*task.due}
                 : std::chrono::sys_days::max();
    return std::make_tuple(
        task.done,
        due,
        -static_cast<int>(task.priority),
        task.title
    );
}

/** Orders a group of tasks in place by `order_key`. */
void sort_section(std::vector<Task>& tasks) {
    std::ranges::sort(tasks, {}, [](const Task& task) {
        return order_key(task);
    });
}

}  // namespace

std::vector<AgendaSection> build_agenda(
    const std::vector<Task>& tasks, std::chrono::year_month_day today
) {
    std::vector<Task> overdue;
    std::vector<Task> inbox;
    std::vector<Task> without_date;
    // Ordered map so day sections come out ascending without an extra sort.
    std::map<std::chrono::year_month_day, std::vector<Task>> dated;

    for(const auto& task : tasks) {
        if(task.due) {
            if(*task.due < today) {
                overdue.push_back(task);
            } else {
                dated[*task.due].push_back(task);
            }
            continue;
        }
        if(task.someday) {
            without_date.push_back(task);
        } else {
            inbox.push_back(task);
        }
    }

    std::vector<AgendaSection> sections;

    if(!overdue.empty()) {
        sort_section(overdue);
        sections.push_back({
            .kind  = SectionKind::OVERDUE,
            .day   = std::nullopt,
            .tasks = std::move(overdue),
        });
    }
    if(!inbox.empty()) {
        sort_section(inbox);
        sections.push_back({
            .kind  = SectionKind::INBOX,
            .day   = std::nullopt,
            .tasks = std::move(inbox),
        });
    }
    for(auto& [day, day_tasks] : dated) {
        sort_section(day_tasks);
        sections.push_back({
            .kind  = SectionKind::DATED,
            .day   = day,
            .tasks = std::move(day_tasks),
        });
    }
    if(!without_date.empty()) {
        sort_section(without_date);
        sections.push_back({
            .kind  = SectionKind::WITHOUT_DATE,
            .day   = std::nullopt,
            .tasks = std::move(without_date),
        });
    }

    return sections;
}

}  // namespace mdtask
