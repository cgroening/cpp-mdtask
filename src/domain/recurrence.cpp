#include "domain/recurrence.hpp"

#include "util/date.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>

namespace mdtask {

namespace {

// A generous cap on stepping loops; guards against a malformed rule (which
// parsing should already reject) ever spinning forever.
constexpr int MAX_STEPS = 4000;

/** Lowercases an ASCII string. */
std::string lower(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for(const char character : text) {
        out.push_back(
            static_cast<char>(std::tolower(static_cast<unsigned char>(character)))
        );
    }
    return out;
}

/** Trims surrounding ASCII whitespace. */
std::string trim(std::string_view text) {
    const auto begin = text.find_first_not_of(" \t\r\n");
    if(begin == std::string_view::npos) {
        return {};
    }
    const auto end = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(begin, end - begin + 1));
}

/** Splits on a delimiter, trimming each piece (empty pieces are dropped). */
std::vector<std::string> split(std::string_view text, char delimiter) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    for(;;) {
        const auto pos = text.find(delimiter, start);
        const auto piece =
            text.substr(start, pos == std::string_view::npos ? pos : pos - start);
        if(std::string trimmed = trim(piece); !trimmed.empty()) {
            parts.push_back(std::move(trimmed));
        }
        if(pos == std::string_view::npos) {
            break;
        }
        start = pos + 1;
    }
    return parts;
}

/** The seven weekday tokens in Monday-first order. */
constexpr std::array<std::pair<std::string_view, std::chrono::weekday>, 7>
    WEEKDAY_NAMES{{
        {"mon", std::chrono::Monday},    {"tue", std::chrono::Tuesday},
        {"wed", std::chrono::Wednesday}, {"thu", std::chrono::Thursday},
        {"fri", std::chrono::Friday},    {"sat", std::chrono::Saturday},
        {"sun", std::chrono::Sunday},
    }};

/** Parses a three-letter weekday token, or std::nullopt when unknown. */
std::optional<std::chrono::weekday> parse_weekday(std::string_view token) {
    for(const auto& [name, day] : WEEKDAY_NAMES) {
        if(token == name) {
            return day;
        }
    }
    return std::nullopt;
}

/** Three-letter token for a weekday. */
std::string_view weekday_name(std::chrono::weekday day) {
    for(const auto& [name, candidate] : WEEKDAY_NAMES) {
        if(candidate == day) {
            return name;
        }
    }
    return "mon";   // unreachable for a valid weekday
}

/** Maps a unit word (singular or plural) onto its enum, or std::nullopt. */
std::optional<RecurUnit> parse_unit(std::string_view word) {
    if(word == "day" || word == "days")     { return RecurUnit::DAY; }
    if(word == "week" || word == "weeks")   { return RecurUnit::WEEK; }
    if(word == "month" || word == "months") { return RecurUnit::MONTH; }
    if(word == "year" || word == "years")   { return RecurUnit::YEAR; }
    return std::nullopt;
}

/** Parses a positive integer, or std::nullopt when not a clean number >= 1. */
std::optional<int> parse_positive(std::string_view text) {
    int value = 0;
    const auto* const begin = text.data();
    const auto* const end = begin + text.size();
    const auto result = std::from_chars(begin, end, value);
    if(result.ec != std::errc{} || result.ptr != end || value < 1) {
        return std::nullopt;
    }
    return value;
}

/** The next occurrence strictly after `base`, one step of the rule. */
std::chrono::year_month_day step_once(
    const RecurrenceRule& rule, std::chrono::year_month_day base
) {
    if(!rule.weekdays.empty()) {
        for(int offset = 1; offset <= 7; ++offset) {
            const auto candidate = shift_days(base, offset);
            const std::chrono::weekday weekday{std::chrono::sys_days{candidate}};
            if(std::ranges::find(rule.weekdays, weekday) != rule.weekdays.end()) {
                return candidate;
            }
        }
        return shift_days(base, 7);   // unreachable for a non-empty set
    }
    switch(rule.unit) {
        case RecurUnit::DAY:   return shift_days(base, rule.interval);
        case RecurUnit::WEEK:  return shift_days(base, 7 * rule.interval);
        case RecurUnit::MONTH: return add_months(base, rule.interval);
        case RecurUnit::YEAR:  return add_years(base, rule.interval);
    }
    return base;   // unreachable
}

}  // namespace

std::optional<RecurrenceRule> parse_recurrence(
    std::string_view repeat_in, std::string_view from_in
) {
    const std::string repeat = lower(trim(repeat_in));
    if(repeat.empty()) {
        return std::nullopt;
    }
    const RecurBasis basis = lower(trim(from_in)) == "completion"
        ? RecurBasis::COMPLETION
        : RecurBasis::DUE;
    const auto interval_rule = [&](RecurUnit unit, int interval) {
        return RecurrenceRule{
            .unit = unit, .interval = interval, .weekdays = {}, .basis = basis
        };
    };

    if(repeat == "daily")   { return interval_rule(RecurUnit::DAY, 1); }
    if(repeat == "weekly")  { return interval_rule(RecurUnit::WEEK, 1); }
    if(repeat == "monthly") { return interval_rule(RecurUnit::MONTH, 1); }
    if(repeat == "yearly" || repeat == "annually") {
        return interval_rule(RecurUnit::YEAR, 1);
    }

    // "every [N] <unit>" - the count is optional and defaults to 1.
    if(repeat.starts_with("every")) {
        const auto words = split(repeat.substr(std::string_view("every").size()),
                                 ' ');
        if(words.size() == 1) {
            if(const auto unit = parse_unit(words[0])) {
                return interval_rule(*unit, 1);
            }
        } else if(words.size() == 2) {
            const auto count = parse_positive(words[0]);
            const auto unit = parse_unit(words[1]);
            if(count && unit) {
                return interval_rule(*unit, *count);
            }
        }
        return std::nullopt;
    }

    // Weekday set (one or more comma-separated tokens).
    const auto tokens = split(repeat, ',');
    if(tokens.empty()) {
        return std::nullopt;
    }
    std::vector<std::chrono::weekday> weekdays;
    for(const auto& token : tokens) {
        const auto day = parse_weekday(token);
        if(!day) {
            return std::nullopt;   // any unknown token rejects the whole rule
        }
        if(std::ranges::find(weekdays, *day) == weekdays.end()) {
            weekdays.push_back(*day);
        }
    }
    std::ranges::sort(weekdays, {}, [](std::chrono::weekday day) {
        return day.iso_encoding();
    });
    return RecurrenceRule{
        .unit = RecurUnit::DAY, .interval = 1,
        .weekdays = std::move(weekdays), .basis = basis
    };
}

std::string format_recurrence(const RecurrenceRule& rule) {
    if(!rule.weekdays.empty()) {
        std::string out;
        for(const auto& day : rule.weekdays) {
            if(!out.empty()) {
                out.push_back(',');
            }
            out += weekday_name(day);
        }
        return out;
    }
    if(rule.interval == 1) {
        switch(rule.unit) {
            case RecurUnit::DAY:   return "daily";
            case RecurUnit::WEEK:  return "weekly";
            case RecurUnit::MONTH: return "monthly";
            case RecurUnit::YEAR:  return "yearly";
        }
    }
    std::string unit;
    switch(rule.unit) {
        case RecurUnit::DAY:   unit = "days";   break;
        case RecurUnit::WEEK:  unit = "weeks";  break;
        case RecurUnit::MONTH: unit = "months"; break;
        case RecurUnit::YEAR:  unit = "years";  break;
    }
    return "every " + std::to_string(rule.interval) + " " + unit;
}

std::chrono::year_month_day next_occurrence(
    const RecurrenceRule& rule,
    std::chrono::year_month_day base,
    std::chrono::year_month_day today
) {
    auto next = step_once(rule, base);
    for(int guard = 0; next <= today && guard < MAX_STEPS; ++guard) {
        next = step_once(rule, next);
    }
    return next;
}

std::vector<std::chrono::year_month_day> upcoming_occurrences(
    const RecurrenceRule& rule,
    std::chrono::year_month_day from_due,
    std::chrono::year_month_day today,
    std::chrono::year_month_day window_end
) {
    std::vector<std::chrono::year_month_day> result;

    // Advance past any overdue instances to the first occurrence today-or-later.
    auto cursor = from_due;
    for(int guard = 0; cursor < today && guard < MAX_STEPS; ++guard) {
        cursor = step_once(rule, cursor);
    }
    result.push_back(cursor);   // guaranteed: at least the next occurrence

    for(int guard = 0; guard < MAX_STEPS; ++guard) {
        const auto next = step_once(rule, cursor);
        if(next > window_end) {
            break;
        }
        result.push_back(next);
        cursor = next;
    }
    return result;
}

}  // namespace mdtask
