#include "tui/task_presentation.hpp"

#include "domain/task.hpp"

#include "check.hpp"
#include "test_suite.hpp"

using namespace mdtask;

namespace {

std::chrono::year_month_day ymd(int year, unsigned month, unsigned day) {
    return std::chrono::year_month_day{
        std::chrono::year{year},
        std::chrono::month{month},
        std::chrono::day{day},
    };
}

}  // namespace

void run_task_presentation_tests() {
    const auto today = ymd(2026, 6, 8);

    // Relative-due offsets: 0 today, +N future, -N overdue.
    {
        CHECK(presentation::format_relative_due(today, today) == "0");
        CHECK(presentation::format_relative_due(ymd(2026, 6, 9), today)
              == "+1");
        CHECK(presentation::format_relative_due(ymd(2026, 6, 7), today)
              == "-1");
        CHECK(presentation::format_relative_due(ymd(2026, 6, 11), today)
              == "+3");
        CHECK(presentation::format_relative_due(ymd(2026, 6, 6), today)
              == "-2");
    }

    // Priority labels map to their plain names.
    {
        CHECK(presentation::priority_label(Priority::HIGH) == "high");
        CHECK(presentation::priority_label(Priority::MEDIUM) == "medium");
        CHECK(presentation::priority_label(Priority::LOW) == "low");
        CHECK(presentation::priority_label(Priority::NONE) == "none");
    }

    // The selection marker is a glyph when marked, blank otherwise.
    {
        CHECK(presentation::selection_symbol(true) == "\xe2\x96\xb8");   // ▸
        CHECK(presentation::selection_symbol(false) == " ");
    }

    // The section header carries the open/done suffix (cancelled counts done).
    {
        AgendaSection section;
        section.kind = SectionKind::INBOX;
        Task open;
        open.status = Status::OPEN;
        Task progress;
        progress.status = Status::IN_PROGRESS;
        Task done;
        done.status = Status::DONE;
        Task cancelled;
        cancelled.status = Status::CANCELLED;
        section.tasks = {open, progress, done, cancelled};

        const std::string header =
            presentation::section_header(section, today, DateFormat::DMY);
        CHECK(header == "Inbox (open: 2; done: 2)");
    }
}
