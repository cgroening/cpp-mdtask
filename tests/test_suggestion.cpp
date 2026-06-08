#include "domain/suggestion.hpp"

#include "domain/task.hpp"

#include "check.hpp"
#include "test_suite.hpp"

#include <optional>

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
    std::string id,
    std::optional<std::chrono::year_month_day> due,
    Priority priority = Priority::NONE,
    std::optional<int> order = std::nullopt
) {
    Task task;
    task.id = id;
    task.title = std::move(id);
    task.due = due;
    task.priority = priority;
    task.order = order;
    task.status = Status::OPEN;
    return task;
}

}  // namespace

void run_suggestion_tests() {
    // No candidates yields no suggestion.
    {
        CHECK(!suggest_next_task({}).has_value());
    }

    // Earliest due date wins; overdue beats today beats the future.
    {
        const std::vector<Task> tasks = {
            make("future", ymd(2026, 6, 20)),
            make("overdue", ymd(2026, 6, 1)),
            make("today", ymd(2026, 6, 8)),
        };
        const auto next = suggest_next_task(tasks);
        CHECK(next.has_value());
        CHECK(next->id == "overdue");
    }

    // Same due date: higher priority wins.
    {
        const std::vector<Task> tasks = {
            make("low", ymd(2026, 6, 8), Priority::LOW),
            make("high", ymd(2026, 6, 8), Priority::HIGH),
            make("medium", ymd(2026, 6, 8), Priority::MEDIUM),
        };
        const auto next = suggest_next_task(tasks);
        CHECK(next.has_value());
        CHECK(next->id == "high");
    }

    // Same due + priority: lower manual order wins (unset sorts last).
    {
        const std::vector<Task> tasks = {
            make("noorder", ymd(2026, 6, 8), Priority::HIGH, std::nullopt),
            make("order5", ymd(2026, 6, 8), Priority::HIGH, 5),
            make("order2", ymd(2026, 6, 8), Priority::HIGH, 2),
        };
        const auto next = suggest_next_task(tasks);
        CHECK(next.has_value());
        CHECK(next->id == "order2");
    }

    // Same due + priority + order: title breaks the tie.
    {
        const std::vector<Task> tasks = {
            make("beta", ymd(2026, 6, 8), Priority::HIGH, 1),
            make("alpha", ymd(2026, 6, 8), Priority::HIGH, 1),
        };
        const auto next = suggest_next_task(tasks);
        CHECK(next.has_value());
        CHECK(next->id == "alpha");
    }

    // Undated tasks rank after any dated task.
    {
        const std::vector<Task> tasks = {
            make("undated", std::nullopt, Priority::HIGH),
            make("dated", ymd(2026, 6, 20), Priority::LOW),
        };
        const auto next = suggest_next_task(tasks);
        CHECK(next.has_value());
        CHECK(next->id == "dated");
    }

    // Done, cancelled, someday and notes are never suggested.
    {
        Task done = make("done", ymd(2026, 6, 1));
        done.status = Status::DONE;
        Task cancelled = make("cancelled", ymd(2026, 6, 1));
        cancelled.status = Status::CANCELLED;
        Task someday = make("someday", std::nullopt);
        someday.someday = true;
        Task note = make("note", ymd(2026, 6, 1));
        note.note = true;
        Task open = make("open", ymd(2026, 6, 10));

        const std::vector<Task> tasks = {done, cancelled, someday, note, open};
        const auto next = suggest_next_task(tasks);
        CHECK(next.has_value());
        CHECK(next->id == "open");
    }

    // Only ineligible tasks -> no suggestion.
    {
        Task done = make("done", ymd(2026, 6, 1));
        done.status = Status::DONE;
        CHECK(!suggest_next_task({done}).has_value());
    }
}
