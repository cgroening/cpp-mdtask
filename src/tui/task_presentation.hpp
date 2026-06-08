#pragma once

#include "config/config.hpp"
#include "domain/agenda.hpp"
#include "domain/task.hpp"

#include <sparcli.hpp>

#include <chrono>
#include <optional>
#include <string>

namespace mdtask::presentation {

/** Formats a date in the configured display format (file names stay ISO). */
[[nodiscard]] std::string format_date(
    std::chrono::year_month_day date, DateFormat format
);

/**
 * Formats the date a task was completed from its ISO `completed_at` timestamp.
 *
 * @return The formatted completion date, or "" when `completed_at` is empty or
 *         not a parseable ISO date.
 */
[[nodiscard]] std::string format_completed(
    const std::optional<std::string>& completed_at, DateFormat format
);

/**
 * Builds the display header for an agenda section, e.g. "OVERDUE", "Inbox",
 * "Today - 05.06.2026", "Tomorrow - 06.06.2026" or a bare formatted date.
 */
[[nodiscard]] std::string section_header(
    const AgendaSection& section,
    std::chrono::year_month_day today,
    DateFormat format
);

/**
 * Color/attributes for a section header: a full-width bar (background) plus
 * light text per category - red overdue, purple inbox, green today, cyan
 * tomorrow, blue later days, gray for the dateless buckets.
 */
[[nodiscard]] sparcli::TextStyle section_style(
    const AgendaSection& section, std::chrono::year_month_day today
);

/** Single-glyph priority marker ("●" for set priorities, blank for NONE). */
[[nodiscard]] std::string priority_symbol(Priority priority);

/** Color/attributes for the priority marker cell. */
[[nodiscard]] sparcli::TextStyle priority_style(Priority priority);

/** Single-glyph status marker (✓ done, ◐ in progress, ⚠ overdue, ○ open). */
[[nodiscard]] std::string status_symbol(const Task& task, bool overdue);

/** Color/attributes for the status cell. */
[[nodiscard]] sparcli::TextStyle status_style(const Task& task, bool overdue);

/** Plain status name ("Open", "In progress", "Done"). */
[[nodiscard]] std::string status_label(Status status);

/** Marker glyph for a note ("✎"), or blank for a regular task. */
[[nodiscard]] std::string note_symbol(const Task& task);

/** Color/attributes for the note marker cell. */
[[nodiscard]] sparcli::TextStyle note_style();

/** Symbol + text for the form's status select (e.g. "◐ In progress"). */
[[nodiscard]] std::string status_choice(Status status);

/** Style for the title cell (dimmed once the task is done). */
[[nodiscard]] sparcli::TextStyle title_style(const Task& task);

/**
 * Relative-due label for a due date, e.g. "today", "tomorrow", "yesterday",
 * "in 3d" or "2d overdue".
 *
 * @param due   The task's due date.
 * @param today The reference day.
 * @return The relative label.
 */
[[nodiscard]] std::string format_relative_due(
    std::chrono::year_month_day due, std::chrono::year_month_day today
);

/** Color/attributes for the relative-due cell (red overdue, yellow soon). */
[[nodiscard]] sparcli::TextStyle relative_due_style(
    std::chrono::year_month_day due, std::chrono::year_month_day today
);

/**
 * Wraps a content line in the app's pinned, full-width header panel (a rounded
 * accent border). Used as the fullscreen header above the finder and the form.
 */
[[nodiscard]] sparcli::Rendered app_header(const sparcli::Text& content);

}  // namespace mdtask::presentation
