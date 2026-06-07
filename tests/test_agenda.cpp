#include "domain/agenda.hpp"
#include "domain/task.hpp"

#include "check.hpp"
#include "test_suite.hpp"

#include <chrono>
#include <string>
#include <vector>

using namespace mdtask;

namespace {

std::chrono::year_month_day ymd(int year, unsigned month, unsigned day) {
    return std::chrono::year_month_day{
        std::chrono::year{year},
        std::chrono::month{month},
        std::chrono::day{day},
    };
}

Task make(
    std::string title,
    std::optional<std::chrono::year_month_day> due,
    Priority priority = Priority::NONE,
    bool done = false,
    bool someday = false
) {
    Task task;
    task.id = title;  // titles are unique in these tests
    task.title = std::move(title);
    task.due = due;
    task.priority = priority;
    task.done = done;
    task.someday = someday;
    return task;
}

}  // namespace

void run_agenda_tests() {
    const auto today = ymd(2026, 6, 6);

    // A full mix exercises section ordering and presence.
    {
        const std::vector<Task> tasks = {
            make("overdue-open", ymd(2026, 6, 4)),
            make("overdue-done", ymd(2026, 6, 3), Priority::NONE, true),
            make("today-low", today, Priority::LOW),
            make("today-high", today, Priority::HIGH),
            make("tomorrow", ymd(2026, 6, 7)),
            make("inbox", std::nullopt),
            make("someday", std::nullopt, Priority::NONE, false, true),
        };

        const auto agenda = build_agenda(tasks, today);
        CHECK(agenda.size() == 5);
        CHECK(agenda[0].kind == SectionKind::OVERDUE);
        CHECK(agenda[1].kind == SectionKind::INBOX);
        CHECK(agenda[2].kind == SectionKind::DATED);
        CHECK(agenda[2].day == today);
        CHECK(agenda[3].kind == SectionKind::DATED);
        CHECK(agenda[3].day == ymd(2026, 6, 7));
        CHECK(agenda[4].kind == SectionKind::WITHOUT_DATE);

        // Overdue keeps open before done.
        CHECK(agenda[0].tasks.size() == 2);
        CHECK(agenda[0].tasks[0].title == "overdue-open");
        CHECK(agenda[0].tasks[1].title == "overdue-done");

        // Within a day, higher priority sorts first.
        CHECK(agenda[2].tasks.size() == 2);
        CHECK(agenda[2].tasks[0].title == "today-high");
        CHECK(agenda[2].tasks[1].title == "today-low");
    }

    // Empty categories are omitted entirely.
    {
        const std::vector<Task> tasks = {
            make("only-today", today),
        };
        const auto agenda = build_agenda(tasks, today);
        CHECK(agenda.size() == 1);
        CHECK(agenda[0].kind == SectionKind::DATED);
        CHECK(agenda[0].day == today);
    }

    // No tasks yields no sections.
    {
        const auto agenda = build_agenda({}, today);
        CHECK(agenda.empty());
    }

    // Distinct future days each get their own ascending section.
    {
        const std::vector<Task> tasks = {
            make("d3", ymd(2026, 6, 9)),
            make("d1", ymd(2026, 6, 7)),
            make("d2", ymd(2026, 6, 8)),
        };
        const auto agenda = build_agenda(tasks, today);
        CHECK(agenda.size() == 3);
        CHECK(agenda[0].day == ymd(2026, 6, 7));
        CHECK(agenda[1].day == ymd(2026, 6, 8));
        CHECK(agenda[2].day == ymd(2026, 6, 9));
    }

    // Beyond next week, dated tasks fall into coarse buckets. With today on
    // 2026-06-06 (Sat) next week ends 2026-06-14 (Sun).
    {
        const std::vector<Task> tasks = {
            make("day", ymd(2026, 6, 10)),         // within next week -> DATED
            make("this-month", ymd(2026, 6, 25)),  // past 06-14, June
            make("next-month", ymd(2026, 7, 15)),  // July
            make("later", ymd(2026, 9, 1)),        // beyond July
        };
        const auto agenda = build_agenda(tasks, today);
        CHECK(agenda.size() == 4);
        CHECK(agenda[0].kind == SectionKind::DATED);
        CHECK(agenda[0].day == ymd(2026, 6, 10));
        CHECK(agenda[1].kind == SectionKind::LATER_THIS_MONTH);
        CHECK(agenda[2].kind == SectionKind::NEXT_MONTH);
        CHECK(agenda[3].kind == SectionKind::LATER);
    }
}
