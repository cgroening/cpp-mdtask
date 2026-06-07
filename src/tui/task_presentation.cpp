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

std::string format_completed(
    const std::optional<std::string>& completed_at, DateFormat format
) {
    if(!completed_at || completed_at->size() < 10) {
        return {};
    }
    const auto date = parse_iso_date(completed_at->substr(0, 10));
    if(!date) {
        return {};
    }
    return format_date(*date, format);
}

std::string section_header(
    const AgendaSection& section,
    std::chrono::year_month_day today,
    DateFormat format
) {
    switch(section.kind) {
        case SectionKind::OVERDUE:          return "OVERDUE";
        case SectionKind::INBOX:            return "Inbox";
        case SectionKind::LATER_THIS_MONTH: return "Later this month";
        case SectionKind::NEXT_MONTH:       return "Next month";
        case SectionKind::LATER:            return "Later";
        case SectionKind::WITHOUT_DATE:     return "Without date";
        case SectionKind::DATED:            break;
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
            return bar(pal::yellow(), pal::yellow_dark());
        case SectionKind::WITHOUT_DATE:
            return bar(pal::fg_darken_2(), pal::bg_lighten_3());
        case SectionKind::LATER_THIS_MONTH:
        case SectionKind::NEXT_MONTH:
        case SectionKind::LATER:
            return bar(pal::blue(), pal::blue_dark());
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

std::string status_symbol(const Task& task, bool overdue) {
    switch(task.status) {
        case Status::DONE:        return "\xe2\x9c\x93";          // ✓
        case Status::IN_PROGRESS: return "\xe2\x97\x90";          // ◐
        case Status::OPEN:        break;
    }
    return overdue ? "\xe2\x9a\xa0" : "\xe2\x97\x8b";   // ⚠ : ○
}

sparcli::TextStyle status_style(const Task& task, bool overdue) {
    switch(task.status) {
        case Status::DONE:
            return sparcli::style(SC_TEXT_ATTR_NONE, sparcli::palette::green());
        case Status::IN_PROGRESS:
            return sparcli::style(
                SC_TEXT_ATTR_BOLD,
                overdue ? sparcli::palette::red() : sparcli::palette::yellow()
            );
        case Status::OPEN:
            break;
    }
    if(overdue) {
        return sparcli::style(SC_TEXT_ATTR_BOLD, sparcli::palette::red());
    }
    return sparcli::style(SC_TEXT_ATTR_DIM);
}

std::string status_label(Status status) {
    switch(status) {
        case Status::IN_PROGRESS: return "In progress";
        case Status::DONE:        return "Done";
        case Status::OPEN:        break;
    }
    return "Open";
}

std::string status_choice(Status status) {
    // Symbol + text for the form's status select (non-overdue glyphs).
    switch(status) {
        case Status::IN_PROGRESS: return "\xe2\x97\x90 In progress";  // ◐
        case Status::DONE:        return "\xe2\x9c\x93 Done";         // ✓
        case Status::OPEN:        break;
    }
    return "\xe2\x97\x8b Open";   // ○
}

sparcli::TextStyle title_style(const Task& task) {
    if(task.status == Status::DONE) {
        return sparcli::style(
            static_cast<ScTextAttribute>(SC_TEXT_ATTR_DIM | SC_TEXT_ATTR_STRIKE)
        );
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
