#include "domain/subtasks.hpp"

namespace mdtask {

namespace {

/** True for a Markdown unordered-list bullet. */
bool is_bullet(char character) {
    return character == '-' || character == '*' || character == '+';
}

/** True for an in-line space or tab (not a newline). */
bool is_inline_space(char character) {
    return character == ' ' || character == '\t';
}

/**
 * Classifies one line as an open box, a done box, or neither.
 *
 * @return 1 for an open checkbox, 2 for a done checkbox, 0 otherwise.
 */
int classify_line(std::string_view line) {
    std::size_t i = 0;
    while(i < line.size() && is_inline_space(line[i])) {
        ++i;
    }
    if(i >= line.size() || !is_bullet(line[i])) {
        return 0;
    }
    ++i;   // the bullet
    if(i >= line.size() || !is_inline_space(line[i])) {
        return 0;
    }
    while(i < line.size() && is_inline_space(line[i])) {
        ++i;
    }
    // Need "[<mark>]" with mark in { ' ', 'x', 'X' }.
    if(i + 2 >= line.size() || line[i] != '[' || line[i + 2] != ']') {
        return 0;
    }
    const char mark = line[i + 1];
    const std::size_t after = i + 3;
    // The box must end the line or be followed by whitespace.
    if(after < line.size() && !is_inline_space(line[after])) {
        return 0;
    }
    if(mark == ' ') {
        return 1;
    }
    if(mark == 'x' || mark == 'X') {
        return 2;
    }
    return 0;
}

}  // namespace

SubtaskProgress count_subtasks(std::string_view body) {
    SubtaskProgress progress;
    std::size_t start = 0;
    while(start <= body.size()) {
        std::size_t end = body.find('\n', start);
        if(end == std::string_view::npos) {
            end = body.size();
        }
        std::string_view line = body.substr(start, end - start);
        if(!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        switch(classify_line(line)) {
            case 1: ++progress.total; break;
            case 2: ++progress.total; ++progress.done; break;
            default: break;
        }
        if(end == body.size()) {
            break;
        }
        start = end + 1;
    }
    return progress;
}

}  // namespace mdtask
