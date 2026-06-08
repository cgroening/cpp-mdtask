#include "domain/agenda.hpp"
#include "domain/recurrence.hpp"

#include "check.hpp"
#include "test_suite.hpp"

#include <chrono>

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

void run_recurrence_tests() {
    using std::chrono::Friday;
    using std::chrono::Monday;
    using std::chrono::Wednesday;

    // Keyword forms parse and round-trip back to their canonical spelling.
    {
        const auto daily = parse_recurrence("daily", "");
        CHECK(daily.has_value());
        CHECK(daily->unit == RecurUnit::DAY);
        CHECK(daily->interval == 1);
        CHECK(daily->weekdays.empty());
        CHECK(daily->basis == RecurBasis::DUE);
        CHECK(format_recurrence(*daily) == "daily");

        CHECK(format_recurrence(*parse_recurrence("weekly", "")) == "weekly");
        CHECK(format_recurrence(*parse_recurrence("monthly", "")) == "monthly");
        CHECK(format_recurrence(*parse_recurrence("yearly", "")) == "yearly");
        // Case-insensitive, and `annually` is an accepted alias for yearly.
        CHECK(format_recurrence(*parse_recurrence("WEEKLY", "")) == "weekly");
        CHECK(format_recurrence(*parse_recurrence("annually", "")) == "yearly");
    }

    // "every N <unit>" parses the count and round-trips; N == 1 collapses to the
    // keyword form.
    {
        const auto three_days = parse_recurrence("every 3 days", "");
        CHECK(three_days.has_value());
        CHECK(three_days->unit == RecurUnit::DAY);
        CHECK(three_days->interval == 3);
        CHECK(format_recurrence(*three_days) == "every 3 days");

        CHECK(format_recurrence(*parse_recurrence("every 2 weeks", ""))
              == "every 2 weeks");
        CHECK(format_recurrence(*parse_recurrence("every 6 months", ""))
              == "every 6 months");
        CHECK(format_recurrence(*parse_recurrence("EVERY 1 YEAR", "")) == "yearly");
        // The count is optional and defaults to 1.
        CHECK(format_recurrence(*parse_recurrence("every week", "")) == "weekly");
    }

    // Weekday sets parse, deduplicate, sort Monday-first and round-trip.
    {
        const auto mwf = parse_recurrence("mon,wed,fri", "");
        CHECK(mwf.has_value());
        CHECK(mwf->weekdays.size() == 3);
        CHECK(mwf->weekdays[0] == Monday);
        CHECK(mwf->weekdays[1] == Wednesday);
        CHECK(mwf->weekdays[2] == Friday);
        CHECK(format_recurrence(*mwf) == "mon,wed,fri");

        // Out-of-order, duplicated and mixed-case input normalizes the same way.
        CHECK(format_recurrence(*parse_recurrence("Fri,Mon,fri", "")) == "mon,fri");
        // A single weekday (no comma) is also a valid set.
        CHECK(format_recurrence(*parse_recurrence("sun", "")) == "sun");
    }

    // repeat_from selects the basis; only `completion` differs from the default.
    {
        CHECK(parse_recurrence("daily", "completion")->basis
              == RecurBasis::COMPLETION);
        CHECK(parse_recurrence("daily", "due")->basis == RecurBasis::DUE);
        CHECK(parse_recurrence("daily", "")->basis == RecurBasis::DUE);
    }

    // Unrecognized input yields no rule.
    {
        CHECK(!parse_recurrence("", "").has_value());
        CHECK(!parse_recurrence("   ", "").has_value());
        CHECK(!parse_recurrence("sometimes", "").has_value());
        CHECK(!parse_recurrence("every blue moon", "").has_value());
        CHECK(!parse_recurrence("monday", "").has_value());   // need the 3-letter form
        CHECK(!parse_recurrence("every 0 days", "").has_value());
        CHECK(!parse_recurrence("mon,funday", "").has_value());
    }

    // next_occurrence on the due basis steps once from a future due date.
    {
        const auto today = ymd(2026, 6, 8);
        const auto daily = *parse_recurrence("daily", "");
        CHECK(next_occurrence(daily, ymd(2026, 6, 10), today) == ymd(2026, 6, 11));

        const auto weekly = *parse_recurrence("weekly", "");
        CHECK(next_occurrence(weekly, ymd(2026, 6, 10), today)
              == ymd(2026, 6, 17));
    }

    // Month and year steps clamp the day to a valid calendar date.
    {
        const auto today = ymd(2026, 1, 1);
        const auto monthly = *parse_recurrence("monthly", "");
        // Jan 31 + 1 month -> Feb 28 in a non-leap year.
        CHECK(next_occurrence(monthly, ymd(2026, 1, 31), today)
              == ymd(2026, 2, 28));
        // Jan 31 + 1 month -> Feb 29 in a leap year.
        CHECK(next_occurrence(monthly, ymd(2024, 1, 31), ymd(2024, 1, 1))
              == ymd(2024, 2, 29));

        const auto yearly = *parse_recurrence("yearly", "");
        // Feb 29 + 1 year -> Feb 28 in the following (non-leap) year.
        CHECK(next_occurrence(yearly, ymd(2024, 2, 29), ymd(2024, 3, 1))
              == ymd(2025, 2, 28));
    }

    // A long-overdue series skips the missed occurrences to the first future one.
    {
        const auto today = ymd(2026, 6, 8);
        const auto daily = *parse_recurrence("daily", "");
        CHECK(next_occurrence(daily, ymd(2026, 6, 1), today) == ymd(2026, 6, 9));

        const auto weekly = *parse_recurrence("weekly", "");
        CHECK(next_occurrence(weekly, ymd(2026, 5, 1), today) == ymd(2026, 6, 12));
    }

    // The completion basis measures from today (caller passes base = today).
    {
        const auto today = ymd(2026, 6, 8);
        const auto three_days = *parse_recurrence("every 3 days", "completion");
        CHECK(next_occurrence(three_days, today, today) == ymd(2026, 6, 11));
    }

    // Weekday mode picks the nearest later day in the set (Mon 8th -> Wed 10th
    // -> Fri 12th -> Mon 15th).
    {
        const auto today = ymd(2026, 6, 8);   // a Monday
        const auto mwf = *parse_recurrence("mon,wed,fri", "");
        CHECK(next_occurrence(mwf, ymd(2026, 6, 8), today) == ymd(2026, 6, 10));
        CHECK(next_occurrence(mwf, ymd(2026, 6, 10), today) == ymd(2026, 6, 12));
        CHECK(next_occurrence(mwf, ymd(2026, 6, 12), today) == ymd(2026, 6, 15));
    }

    // upcoming_occurrences lists the in-window dates for the Recurring view.
    {
        const auto today = ymd(2026, 6, 8);   // Monday
        const auto window_end = end_of_next_week(today);   // Sunday 2026-06-21
        CHECK(window_end == ymd(2026, 6, 21));

        const auto weekly = *parse_recurrence("weekly", "");
        const auto due_dates =
            upcoming_occurrences(weekly, ymd(2026, 6, 10), today, window_end);
        CHECK(due_dates.size() == 2);
        CHECK(due_dates[0] == ymd(2026, 6, 10));
        CHECK(due_dates[1] == ymd(2026, 6, 17));
    }

    // A sparse rule whose next date is past the window still yields that one.
    {
        const auto today = ymd(2026, 6, 8);
        const auto window_end = end_of_next_week(today);
        const auto monthly = *parse_recurrence("monthly", "");
        const auto due_dates =
            upcoming_occurrences(monthly, ymd(2026, 7, 1), today, window_end);
        CHECK(due_dates.size() == 1);
        CHECK(due_dates[0] == ymd(2026, 7, 1));
    }

    // An overdue series starts its projection at the first occurrence today or
    // later, never in the past.
    {
        const auto today = ymd(2026, 6, 8);
        const auto window_end = end_of_next_week(today);
        const auto daily = *parse_recurrence("daily", "");
        const auto due_dates =
            upcoming_occurrences(daily, ymd(2026, 6, 1), today, window_end);
        CHECK(!due_dates.empty());
        CHECK(due_dates.front() == ymd(2026, 6, 8));
        CHECK(due_dates.back() == ymd(2026, 6, 21));
    }
}
