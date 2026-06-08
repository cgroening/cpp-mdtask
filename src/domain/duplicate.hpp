#pragma once

#include <string>
#include <vector>

namespace mdtask {

/**
 * Builds the title for a duplicate of `source_title`, numbered to stay unique.
 *
 * A trailing " (copy)" / " (copy <n>)" on the source is stripped first, so
 * duplicating "X (copy)" yields "X (copy 2)" rather than "X (copy) (copy)".
 * The result is the first of "X (copy)", "X (copy 2)", "X (copy 3)", ... that
 * is not already present in `existing_titles` (gaps are filled).
 *
 * @param source_title   Title of the item being duplicated.
 * @param existing_titles Titles already in use among the same kind of item.
 * @return A unique copy title.
 */
[[nodiscard]] std::string next_copy_title(
    const std::string& source_title,
    const std::vector<std::string>& existing_titles
);

}  // namespace mdtask
