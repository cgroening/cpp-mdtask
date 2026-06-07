#include "tui/task_presentation.hpp"

#include "util/date.hpp"

#include <format>

namespace mdtask::presentation {

namespace {

/** A blank marker keeps the priority column width stable for NONE. */
constexpr const char* NO_PRIORITY_MARKER = " ";
constexpr const char* PRIORITY_MARKER = "●";  // ● filled circle

}  // namespace

std::string format_date(
    std::chrono::year_month_day date, DateFormat format
) {
    if(format == DateFormat::ISO) {
        return to_iso(date);
    }
    return std::format(
        "{:02}.{:02}.{:04}",
        static_cast<unsigned>(date.day()),
        static_cast<unsigned>(date.month()),
        static_cast<int>(date.year())
    );
}

std::string section_header(
    const AgendaSection& section,
    std::chrono::year_month_day today,
    DateFormat format
) {
    switch(section.kind) {
        case SectionKind::OVERDUE:      return "OVERDUE";
        case SectionKind::INBOX:        return "Inbox";
        case SectionKind::WITHOUT_DATE: return "Without date";
        case SectionKind::DATED:        break;
    }

    const auto day = section.day.value_or(today);
    const std::string date = format_date(day, format);
    if(day == today) {
        return "Today - " + date;
    }
    if(day == shift_days(today, 1)) {
        return "Tomorrow - " + date;
    }
    return date;
}

sparcli::TextStyle section_style(
    const AgendaSection& section, std::chrono::year_month_day today
) {
    namespace pal = sparcli::palette;
    const auto bar = [](sparcli::Color fg, sparcli::Color bg) {
        return sparcli::style(SC_TEXT_ATTR_BOLD, fg, bg);
    };

    switch(section.kind) {
        case SectionKind::OVERDUE:
            return bar(pal::red(), pal::red_dark());
        case SectionKind::INBOX:
            return bar(pal::purple(), pal::purple_dark());
        case SectionKind::WITHOUT_DATE:
            return bar(pal::fg_darken_2(), pal::bg_lighten_3());
        case SectionKind::DATED:
            break;
    }

    const auto day = section.day.value_or(today);
    if(day == today) {
        return bar(pal::green(), pal::green_dark());
    }
    if(day == shift_days(today, 1)) {
        return bar(pal::cyan(), pal::cyan_dark());
    }
    return bar(pal::blue(), pal::blue_dark());
}

std::string priority_symbol(Priority priority) {
    return priority == Priority::NONE ? NO_PRIORITY_MARKER : PRIORITY_MARKER;
}

sparcli::TextStyle priority_style(Priority priority) {
    switch(priority) {
        case Priority::HIGH:
            return sparcli::style(SC_TEXT_ATTR_BOLD, sparcli::palette::red());
        case Priority::MEDIUM:
            return sparcli::style(SC_TEXT_ATTR_NONE, sparcli::palette::orange());
        case Priority::LOW:
            return sparcli::style(SC_TEXT_ATTR_NONE, sparcli::palette::blue());
        case Priority::NONE:
            break;
    }
    return sparcli::TextStyle{};
}

std::string status_text(const Task& task, bool overdue) {
    if(task.done) {
        return "done";
    }
    return overdue ? "overdue" : "open";
}

sparcli::TextStyle status_style(const Task& task, bool overdue) {
    if(task.done) {
        return sparcli::style(SC_TEXT_ATTR_NONE, sparcli::palette::green());
    }
    if(overdue) {
        return sparcli::style(SC_TEXT_ATTR_BOLD, sparcli::palette::red());
    }
    return sparcli::style(SC_TEXT_ATTR_DIM);
}

sparcli::TextStyle title_style(const Task& task) {
    if(task.done) {
        return sparcli::style(SC_TEXT_ATTR_DIM);
    }
    return sparcli::TextStyle{};
}

sparcli::Rendered app_header(const sparcli::Text& content) {
    sparcli::PanelOpts opts{};
    opts.border = {
        .type = SC_BORDER_ROUNDED, .color = sparcli::palette::purple()
    };
    opts.full_width = true;
    opts.padding = {.right = 1, .left = 1};
    return sparcli::capture::panel(content, opts);
}

}  // namespace mdtask::presentation
