#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

namespace mdtask {

/**
 * Date helpers shared across layers.
 *
 * Dates are modelled as `std::chrono::year_month_day` (a calendar day without
 * a time zone). The canonical serialized form is ISO `YYYY-MM-DD`; display
 * formatting that depends on user configuration lives in the presentation
 * layer, not here.
 */

/** Returns today's date in the system's local time zone. */
[[nodiscard]] std::chrono::year_month_day today();

/**
 * Parses an ISO `YYYY-MM-DD` date.
 *
 * @param text Candidate string; surrounding whitespace is not trimmed.
 * @return The parsed date, or std::nullopt when `text` is not a valid ISO
 *         date (wrong shape, non-numeric, or an impossible calendar day).
 */
[[nodiscard]] std::optional<std::chrono::year_month_day> parse_iso_date(
    std::string_view text
);

/** Formats a date as ISO `YYYY-MM-DD`. */
[[nodiscard]] std::string to_iso(std::chrono::year_month_day date);

/**
 * Shifts a date by a (possibly negative) number of days, normalizing the
 * result to a real calendar day.
 *
 * @param date  Starting day.
 * @param days  Offset in days (negative moves into the past).
 * @return The resulting calendar day.
 */
[[nodiscard]] std::chrono::year_month_day shift_days(
    std::chrono::year_month_day date, int days
);

/** Whole-day difference `to - from` (positive when `to` is in the future). */
[[nodiscard]] int days_between(
    std::chrono::year_month_day from, std::chrono::year_month_day to
);

/** Current local time as an ISO `YYYY-MM-DDTHH:MM:SS` timestamp. */
[[nodiscard]] std::string now_timestamp();

}  // namespace mdtask
