#include "domain/duplicate.hpp"

#include <algorithm>
#include <format>
#include <string_view>

namespace mdtask {

namespace {

/** True when every character of `text` is an ASCII digit (and it is non-empty). */
bool all_digits(std::string_view text) {
    return !text.empty()
        && std::ranges::all_of(text, [](unsigned char c) {
               return c >= '0' && c <= '9';
           });
}

/**
 * Strips a trailing " (copy)" or " (copy <n>)" from `title`, returning the base
 * title. Anything else is returned unchanged.
 */
std::string strip_copy_suffix(const std::string& title) {
    constexpr std::string_view plain = " (copy)";
    if(title.ends_with(plain)) {
        return title.substr(0, title.size() - plain.size());
    }
    constexpr std::string_view open = " (copy ";
    if(title.ends_with(")")) {
        const auto pos = title.rfind(open);
        if(pos != std::string::npos) {
            const std::string_view number(
                title.data() + pos + open.size(),
                title.size() - pos - open.size() - 1   // drop the ')'
            );
            if(all_digits(number)) {
                return title.substr(0, pos);
            }
        }
    }
    return title;
}

}  // namespace

std::string next_copy_title(
    const std::string& source_title,
    const std::vector<std::string>& existing_titles
) {
    const std::string base = strip_copy_suffix(source_title);
    for(int n = 1;; ++n) {
        const std::string candidate = n == 1
            ? base + " (copy)"
            : std::format("{} (copy {})", base, n);
        if(std::ranges::find(existing_titles, candidate)
           == existing_titles.end()) {
            return candidate;
        }
    }
}

}  // namespace mdtask
