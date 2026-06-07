#include "domain/agenda.hpp"

#include "util/date.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <string>
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
        task.status == Status::DONE,
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

constexpr const char* MONTH_NAMES[] = {
    "January", "February", "March",     "April",   "May",      "June",
    "July",    "August",   "September", "October", "November", "December",
};

/** Calendar day a task was completed, parsed from `completed_at` (or nullopt). */
std::optional<std::chrono::year_month_day> completion_day(const Task& task) {
    if(!task.completed_at || task.completed_at->size() < 10) {
        return std::nullopt;
    }
    return parse_iso_date(task.completed_at->substr(0, 10));
}

/** Orders archived tasks newest-completed first, then by title. */
void sort_archive_group(std::vector<Task>& tasks) {
    std::ranges::sort(tasks, [](const Task& a, const Task& b) {
        const auto da = completion_day(a);
        const auto db = completion_day(b);
        if(da != db) {
            return da > db;   // most recent completion first
        }
        return a.title < b.title;
    });
}

}  // namespace

std::vector<AgendaSection> build_agenda(
    const std::vector<Task>& tasks, std::chrono::year_month_day today
) {
    using namespace std::chrono;

    // Individual day sections run from today through the end of next week
    // (weeks start Monday); dated tasks beyond that fall into coarse buckets.
    const sys_days today_days{today};
    const int to_sunday = 7 - static_cast<int>(weekday{today_days}.iso_encoding());
    const year_month_day end_next_week{today_days + days{to_sunday + 7}};
    const year_month this_month{today.year(), today.month()};
    const year_month next_month = this_month + months{1};

    std::vector<Task> overdue;
    std::vector<Task> inbox;
    std::vector<Task> later_this_month;
    std::vector<Task> next_month_tasks;
    std::vector<Task> later;
    std::vector<Task> without_date;
    // Ordered map so day sections come out ascending without an extra sort.
    std::map<year_month_day, std::vector<Task>> dated;

    for(const auto& task : tasks) {
        if(task.due) {
            const year_month_day due = *task.due;
            const year_month due_month{due.year(), due.month()};
            if(due < today) {
                overdue.push_back(task);
            } else if(due <= end_next_week) {
                dated[due].push_back(task);
            } else if(due_month == this_month) {
                later_this_month.push_back(task);
            } else if(due_month == next_month) {
                next_month_tasks.push_back(task);
            } else {
                later.push_back(task);
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
    const auto add = [&](SectionKind kind, std::vector<Task>& group) {
        if(!group.empty()) {
            sort_section(group);
            sections.push_back({
                .kind  = kind,
                .day   = std::nullopt,
                .tasks = std::move(group),
            });
        }
    };

    add(SectionKind::OVERDUE, overdue);
    add(SectionKind::INBOX, inbox);
    for(auto& [day, day_tasks] : dated) {
        sort_section(day_tasks);
        sections.push_back({
            .kind  = SectionKind::DATED,
            .day   = day,
            .tasks = std::move(day_tasks),
        });
    }
    add(SectionKind::LATER_THIS_MONTH, later_this_month);
    add(SectionKind::NEXT_MONTH, next_month_tasks);
    add(SectionKind::LATER, later);
    add(SectionKind::WITHOUT_DATE, without_date);

    return sections;
}

std::vector<ArchiveGroup> group_archive(
    const std::vector<Task>& archived, std::chrono::year_month_day today
) {
    using std::chrono::year_month;
    const year_month this_month{today.year(), today.month()};
    const year_month oldest_month = this_month - std::chrono::months{11};

    // Descending maps: most recent month/year first.
    std::map<year_month, std::vector<Task>, std::greater<>> months;
    std::map<int, std::vector<Task>, std::greater<>> years;
    std::vector<Task> no_date;

    for(const auto& task : archived) {
        const auto day = completion_day(task);
        if(!day) {
            no_date.push_back(task);
            continue;
        }
        const year_month completed{day->year(), day->month()};
        if(completed >= oldest_month) {
            months[completed].push_back(task);
        } else {
            years[static_cast<int>(day->year())].push_back(task);
        }
    }

    std::vector<ArchiveGroup> groups;
    for(auto& [month, tasks] : months) {
        sort_archive_group(tasks);
        const unsigned index = static_cast<unsigned>(month.month()) - 1;
        groups.push_back({
            .header = std::string(MONTH_NAMES[index]) + " "
                + std::to_string(static_cast<int>(month.year())),
            .tasks  = std::move(tasks),
        });
    }
    for(auto& [year, tasks] : years) {
        sort_archive_group(tasks);
        groups.push_back({.header = std::to_string(year),
                          .tasks = std::move(tasks)});
    }
    if(!no_date.empty()) {
        sort_archive_group(no_date);
        groups.push_back({.header = "No completion date",
                          .tasks = std::move(no_date)});
    }
    return groups;
}

}  // namespace mdtask
