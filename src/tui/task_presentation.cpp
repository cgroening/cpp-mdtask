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

namespace {

/** Full weekday name in the given language (Monday-first via iso_encoding). */
std::string weekday_name(std::chrono::year_month_day day, Language language) {
    static constexpr const char* ENGLISH[] = {
        "Monday", "Tuesday", "Wednesday", "Thursday",
        "Friday", "Saturday", "Sunday",
    };
    static constexpr const char* GERMAN[] = {
        "Montag", "Dienstag", "Mittwoch", "Donnerstag",
        "Freitag", "Samstag", "Sonntag",
    };
    const unsigned iso =
        std::chrono::weekday{std::chrono::sys_days{day}}.iso_encoding();
    const unsigned index = iso - 1;   // 1..7 (Mon..Sun) -> 0..6
    return language == Language::GERMAN ? GERMAN[index] : ENGLISH[index];
}

/** A date with its weekday prefixed, e.g. "Monday, 08.06.2026". */
std::string weekday_date(
    std::chrono::year_month_day day, DateFormat format, Language language
) {
    return weekday_name(day, language) + ", " + format_date(day, format);
}

/** Base label for a section, without the open/done count suffix. */
std::string section_label(
    const AgendaSection& section,
    std::chrono::year_month_day today,
    DateFormat format,
    Language language
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
    const std::string labelled = weekday_date(day, format, language);
    if(day == today) {
        return "Today - " + labelled;
    }
    if(day == shift_days(today, 1)) {
        return "Tomorrow - " + labelled;
    }
    return labelled;
}

}  // namespace

std::string section_header(
    const AgendaSection& section,
    std::chrono::year_month_day today,
    DateFormat format,
    Language language
) {
    const SectionCounts counts = count_section(section);
    return std::format(
        "{} (open: {}; done: {})",
        section_label(section, today, format, language), counts.open,
        counts.done
    );
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
        case Status::CANCELLED:   return "\xe2\x8a\x98";          // ⊘
        case Status::IN_PROGRESS: return "\xe2\x97\x90";          // ◐
        case Status::PAUSED:      return "\xe2\x80\x96";          // ‖
        case Status::OPEN:        break;
    }
    return overdue ? "\xe2\x9a\xa0" : "\xe2\x97\x8b";   // ⚠ : ○
}

sparcli::TextStyle status_style(const Task& task, bool overdue) {
    switch(task.status) {
        case Status::DONE:
            return sparcli::style(SC_TEXT_ATTR_NONE, sparcli::palette::green());
        case Status::CANCELLED:
            // Own branch so it can diverge from done later; green for now.
            return sparcli::style(SC_TEXT_ATTR_NONE, sparcli::palette::green());
        case Status::IN_PROGRESS:
            return sparcli::style(
                SC_TEXT_ATTR_BOLD,
                overdue ? sparcli::palette::red() : sparcli::palette::yellow()
            );
        case Status::PAUSED:
            return sparcli::style(
                SC_TEXT_ATTR_BOLD,
                overdue ? sparcli::palette::red() : sparcli::palette::blue()
            );
        case Status::OPEN:
            break;
    }
    if(overdue) {
        return sparcli::style(SC_TEXT_ATTR_BOLD, sparcli::palette::red());
    }
    return sparcli::style(SC_TEXT_ATTR_DIM);
}

std::string note_symbol(const Task& task) {
    return task.note ? "\xe2\x9c\x8e" : " ";   // ✎
}

sparcli::TextStyle note_style() {
    return sparcli::style(SC_TEXT_ATTR_NONE, sparcli::palette::cyan());
}

std::string selection_symbol(bool marked) {
    return marked ? "\xe2\x96\xb8" : " ";   // ▸
}

sparcli::TextStyle selection_style() {
    return sparcli::style(SC_TEXT_ATTR_BOLD, sparcli::palette::yellow());
}

std::string status_label(Status status) {
    switch(status) {
        case Status::IN_PROGRESS: return "In progress";
        case Status::PAUSED:      return "Paused";
        case Status::DONE:        return "Done";
        case Status::CANCELLED:   return "Cancelled";
        case Status::OPEN:        break;
    }
    return "Open";
}

std::string priority_label(Priority priority) {
    switch(priority) {
        case Priority::HIGH:   return "high";
        case Priority::MEDIUM: return "medium";
        case Priority::LOW:    return "low";
        case Priority::NONE:   break;
    }
    return "none";
}

std::string status_choice(Status status) {
    // Symbol + text for the form's status select (non-overdue glyphs).
    switch(status) {
        case Status::IN_PROGRESS: return "\xe2\x97\x90 In progress";  // ◐
        case Status::PAUSED:      return "\xe2\x80\x96 Paused";       // ‖
        case Status::DONE:        return "\xe2\x9c\x93 Done";         // ✓
        case Status::CANCELLED:   return "\xe2\x8a\x98 Cancelled";    // ⊘
        case Status::OPEN:        break;
    }
    return "\xe2\x97\x8b Open";   // ○
}

sparcli::TextStyle title_style(const Task& task) {
    if(is_terminal(task.status)) {
        return sparcli::style(
            static_cast<ScTextAttribute>(SC_TEXT_ATTR_DIM | SC_TEXT_ATTR_STRIKE)
        );
    }
    return sparcli::TextStyle{};
}

std::string format_relative_due(
    std::chrono::year_month_day due, std::chrono::year_month_day today
) {
    const int days = days_between(today, due);
    if(days > 0) {
        return std::format("+{}", days);   // future: +1, +2, ...
    }
    return std::format("{}", days);        // today: 0; overdue: -1, -2, ...
}

sparcli::TextStyle relative_due_style(
    std::chrono::year_month_day due, std::chrono::year_month_day today
) {
    const int days = days_between(today, due);
    if(days < 0) {
        return sparcli::style(SC_TEXT_ATTR_BOLD, sparcli::palette::red());
    }
    if(days == 0) {
        return sparcli::style(SC_TEXT_ATTR_NONE, sparcli::palette::yellow());
    }
    return sparcli::style(SC_TEXT_ATTR_DIM);   // future days in gray
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
