#pragma once

#include <chrono>
#include <optional>
#include <string>

namespace mdtask {

/** Task priority, lowest to highest; drives ordering and color in the agenda. */
enum class Priority {
    NONE,
    LOW,
    MEDIUM,
    HIGH,
};

/** Lifecycle state of a task; "overdue" is derived (not stored). */
enum class Status {
    OPEN,
    IN_PROGRESS,
    DONE,
};

/**
 * A single task: the central domain entity, backed by one Markdown file.
 *
 * A plain value type with no behaviour and no dependencies on other layers.
 * The title is mirrored both as the front-matter `title` and as the body's H1;
 * the rest of the body is the free-form `description`. Rules that act on tasks
 * (validation, state transitions, id and filename generation) live in the
 * service and storage layers.
 */
struct Task {
    /** Stable id; the single source of truth for all linking. */
    std::string id;

    /** Display title (front-matter `title`, mirrored as the body H1). */
    std::string title;

    /** Free-form Markdown body below the H1 (may be empty). */
    std::string description;

    /** Due date; empty means the task has no date (Inbox or someday). */
    std::optional<std::chrono::year_month_day> due;

    /** Priority bucket. */
    Priority priority = Priority::NONE;

    /** True marks a dateless task as deliberately "someday" (WITHOUT DATE). */
    bool someday = false;

    /** Lifecycle state; a DONE task stays in place until archived. */
    Status status = Status::OPEN;

    /** ISO timestamp recorded while the task is DONE (empty otherwise). */
    std::optional<std::string> completed_at;

    /** Creation date (immutable once set). */
    std::chrono::year_month_day created{};

    /** Owning project's id; empty means the task has no project. */
    std::string project;
};

}  // namespace mdtask
