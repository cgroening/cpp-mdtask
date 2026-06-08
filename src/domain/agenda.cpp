#include "domain/agenda.hpp"

#include "util/date.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>

namespace mdtask {

namespace {

/**
 * Orders two tasks within a section: active (not done) before done; active by
 * the manual `order` (unset sorts last, then priority desc, then title); done by
 * completion time (newest-done last, then title).
 */
bool section_less(const Task& a, const Task& b) {
    const bool a_done = is_terminal(a.status);
    const bool b_done = is_terminal(b.status);
    if(a_done != b_done) {
        return !a_done;   // active tasks come first
    }

    if(!a_done) {
        if(a.order != b.order) {
            if(!a.order) { return false; }   // unset order sorts to the bottom
            if(!b.order) { return true; }
            return *a.order < *b.order;
        }
        if(a.priority != b.priority) {
            return static_cast<int>(a.priority) > static_cast<int>(b.priority);
        }
        return a.title < b.title;
    }

    // Both done: oldest completion first, so freshly done tasks fall to the end.
    if(a.completed_at != b.completed_at) {
        if(!a.completed_at) { return false; }
        if(!b.completed_at) { return true; }
        return *a.completed_at < *b.completed_at;
    }
    return a.title < b.title;
}

/** Orders a group of tasks in place for display. */
void sort_section(std::vector<Task>& tasks) {
    std::ranges::sort(tasks, section_less);
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

SectionCounts count_section(const AgendaSection& section) {
    SectionCounts counts;
    for(const auto& task : section.tasks) {
        if(is_terminal(task.status)) {
            ++counts.done;
        } else {
            ++counts.open;
        }
    }
    return counts;
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
