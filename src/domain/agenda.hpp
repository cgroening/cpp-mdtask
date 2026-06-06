#pragma once

#include "domain/task.hpp"

#include <chrono>
#include <optional>
#include <vector>

namespace mdtask {

/** Which kind of agenda section a group of tasks belongs to. */
enum class SectionKind {
    OVERDUE,       /**< Has a due date before today (done or open). */
    INBOX,         /**< No due date and not marked someday. */
    DATED,         /**< A specific calendar day from today onward. */
    WITHOUT_DATE,  /**< No due date, deliberately marked someday. */
};

/**
 * One non-empty, ordered group of tasks in the agenda.
 *
 * The display header text is built by the presentation layer (it depends on
 * the user's date format and on `day`); this domain type only carries the
 * structural information needed to render and order the agenda.
 */
struct AgendaSection {
    SectionKind kind;                                /**< Section category. */
    std::optional<std::chrono::year_month_day> day;  /**< Set for DATED only. */
    std::vector<Task> tasks;                         /**< Ordered group members. */
};

/**
 * Groups tasks into ordered, non-empty agenda sections relative to `today`.
 *
 * Section order: OVERDUE, INBOX, one DATED section per distinct due day from
 * today onward (ascending), then WITHOUT_DATE. Empty sections are omitted.
 * Within each section tasks are ordered open-before-done, then by due date
 * ascending, then by priority descending, then by title.
 *
 * @param tasks Tasks to group (not mutated).
 * @param today The reference day used to classify due dates.
 * @return The ordered, non-empty sections; empty when `tasks` is empty.
 */
[[nodiscard]] std::vector<AgendaSection> build_agenda(
    const std::vector<Task>& tasks, std::chrono::year_month_day today
);

}  // namespace mdtask
