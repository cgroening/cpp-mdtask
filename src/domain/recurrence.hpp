#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mdtask {

/**
 * Recurrence support for tasks: a small value type plus the date math that
 * advances a series. Kept free of any dependency on Task so it can seed a
 * future reusable scheduling library; the rules that connect it to a task
 * (when to roll forward) live in the service layer.
 */

/** Time unit for an interval-based recurrence (`every N units`). */
enum class RecurUnit { DAY, WEEK, MONTH, YEAR };

/** Whether the next occurrence is measured from the due date or completion. */
enum class RecurBasis { DUE, COMPLETION };

/**
 * A recurrence rule: either an interval (`every N units`) or a set of weekdays.
 *
 * When `weekdays` is non-empty the rule is in weekday mode and the next
 * occurrence is the nearest later day in that set; `unit`/`interval` are then
 * ignored. Otherwise the rule advances by `interval` whole `unit`s.
 */
struct RecurrenceRule {
    RecurUnit unit = RecurUnit::DAY;   /**< Interval unit (interval mode). */
    int interval = 1;                  /**< Step size >= 1 (interval mode). */
    /** Weekday set; non-empty selects weekday mode. */
    std::vector<std::chrono::weekday> weekdays;
    RecurBasis basis = RecurBasis::DUE;   /**< Schedule basis. */
};

/**
 * Parses the `repeat:` and `repeat_from:` front-matter values into a rule.
 *
 * Accepted `repeat` forms (case-insensitive): `daily`, `weekly`, `monthly`,
 * `yearly`; `every N days|weeks|months|years` (N optional, defaults to 1); and
 * a comma list of weekdays `mon,tue,wed,thu,fri,sat,sun`. `from` is `completion`
 * for the completion basis, anything else (including empty) for the due basis.
 *
 * @param repeat The `repeat` value; empty or unrecognized yields std::nullopt.
 * @param from   The `repeat_from` value; only `completion` changes the basis.
 * @return The parsed rule, or std::nullopt when `repeat` is not understood.
 */
[[nodiscard]] std::optional<RecurrenceRule> parse_recurrence(
    std::string_view repeat, std::string_view from
);

/**
 * Formats a rule back into its canonical `repeat:` string (round-trips parsing).
 *
 * @param rule The rule to serialize.
 * @return e.g. `daily`, `every 3 days`, `weekly`, or `mon,wed,fri`.
 */
[[nodiscard]] std::string format_recurrence(const RecurrenceRule& rule);

/**
 * Computes the next occurrence strictly after `base`, skipping any that are not
 * after `today` (so a long-overdue series jumps straight to a future date).
 *
 * The caller passes `base = due` for the due basis or `base = today` for the
 * completion basis.
 *
 * @param rule  The recurrence rule.
 * @param base  The reference day to advance from.
 * @param today The day the result must land strictly after.
 * @return The next occurrence date.
 */
[[nodiscard]] std::chrono::year_month_day next_occurrence(
    const RecurrenceRule& rule,
    std::chrono::year_month_day base,
    std::chrono::year_month_day today
);

/**
 * Projects a series' occurrences for an agenda window, for the Recurring view.
 *
 * Returns the occurrences from the first one that is today-or-later through
 * `window_end`, but always at least that first one even when it lands past the
 * window (so a sparse rule still shows its next date).
 *
 * @param rule       The recurrence rule.
 * @param from_due   The series' current due date (its next pending occurrence).
 * @param today      The reference day; earlier occurrences are skipped.
 * @param window_end The last day to include beyond the guaranteed first one.
 * @return Occurrence dates in ascending order (never empty).
 */
[[nodiscard]] std::vector<std::chrono::year_month_day> upcoming_occurrences(
    const RecurrenceRule& rule,
    std::chrono::year_month_day from_due,
    std::chrono::year_month_day today,
    std::chrono::year_month_day window_end
);

}  // namespace mdtask
