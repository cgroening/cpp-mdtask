#include "util/date.hpp"

#include <charconv>
#include <chrono>
#include <ctime>
#include <format>

namespace mdtask {

namespace {

constexpr std::size_t ISO_DATE_LENGTH = 10;  // YYYY-MM-DD

/** Converts a calendar day to a sys_days count for day arithmetic. */
std::chrono::sys_days to_sys_days(std::chrono::year_month_day date) {
    return std::chrono::sys_days{date};
}

/** Parses an unsigned integer from an exact-width substring. */
std::optional<int> parse_uint(std::string_view text) {
    int value = 0;
    const auto* begin = text.data();
    const auto* end = begin + text.size();
    const auto result = std::from_chars(begin, end, value);
    if(result.ec != std::errc{} || result.ptr != end || value < 0) {
        return std::nullopt;
    }
    return value;
}

}  // namespace

std::chrono::year_month_day today() {
    // localtime_r is used instead of std::chrono::current_zone() because the
    // latter's tzdb support is unreliable on macOS/libc++.
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    return std::chrono::year_month_day{
        std::chrono::year{local.tm_year + 1900},
        std::chrono::month{static_cast<unsigned>(local.tm_mon + 1)},
        std::chrono::day{static_cast<unsigned>(local.tm_mday)},
    };
}

std::optional<std::chrono::year_month_day> parse_iso_date(
    std::string_view text
) {
    if(text.size() != ISO_DATE_LENGTH || text[4] != '-' || text[7] != '-') {
        return std::nullopt;
    }
    const auto year = parse_uint(text.substr(0, 4));
    const auto month = parse_uint(text.substr(5, 2));
    const auto day = parse_uint(text.substr(8, 2));
    if(!year || !month || !day) {
        return std::nullopt;
    }

    const std::chrono::year_month_day date{
        std::chrono::year{*year},
        std::chrono::month{static_cast<unsigned>(*month)},
        std::chrono::day{static_cast<unsigned>(*day)},
    };
    if(!date.ok()) {
        return std::nullopt;
    }
    return date;
}

std::string to_iso(std::chrono::year_month_day date) {
    return std::format(
        "{:04}-{:02}-{:02}",
        static_cast<int>(date.year()),
        static_cast<unsigned>(date.month()),
        static_cast<unsigned>(date.day())
    );
}

std::chrono::year_month_day shift_days(
    std::chrono::year_month_day date, int days
) {
    return std::chrono::year_month_day{
        to_sys_days(date) + std::chrono::days{days}
    };
}

int days_between(
    std::chrono::year_month_day from, std::chrono::year_month_day to
) {
    return static_cast<int>((to_sys_days(to) - to_sys_days(from)).count());
}

std::string now_timestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    char buffer[20];  // "YYYY-MM-DDTHH:MM:SS" + NUL
    std::strftime(buffer, sizeof buffer, "%Y-%m-%dT%H:%M:%S", &local);
    return buffer;
}

}  // namespace mdtask
