#pragma once

#include <serde/sparcli_serde.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mdtask {

/**
 * A Markdown file split into its YAML front matter and its body.
 *
 * Deliberately free of any task-specific knowledge: this is the generic
 * "Markdown record" building block, the natural candidate for extraction into
 * a reusable markdown-database library once a second application needs it.
 */
struct MarkdownDocument {
    /** Parsed front matter as an object (an empty object when none present). */
    sparcli::serde::Value frontmatter;

    /** Body text after the front matter (may be empty). */
    std::string body;
};

/**
 * Reads and parses a Markdown file.
 *
 * @param path File to read.
 * @return The document, or std::nullopt when the file does not exist.
 * @throws StorageError on a read or parse failure.
 */
[[nodiscard]] std::optional<MarkdownDocument> read_markdown(
    const std::filesystem::path& path
);

/**
 * Atomically writes a Markdown file: a `---`-fenced YAML front-matter block
 * followed by `body`. The write goes to a temporary file in the same directory
 * and is renamed into place, so a reader never sees a half-written file.
 *
 * @param path        Destination file (parent directories are created).
 * @param frontmatter Front-matter object to serialize as YAML.
 * @param body        Body text to write after the front matter.
 * @throws StorageError on a write failure.
 */
void write_markdown(
    const std::filesystem::path& path,
    const sparcli::serde::Value& frontmatter,
    std::string_view body
);

/**
 * Lists the `*.md` files directly under `dir` (non-recursive).
 *
 * @param dir Directory to scan; a missing directory yields an empty list.
 * @return Matching file paths sorted by filename.
 */
[[nodiscard]] std::vector<std::filesystem::path> list_markdown_files(
    const std::filesystem::path& dir
);

}  // namespace mdtask
