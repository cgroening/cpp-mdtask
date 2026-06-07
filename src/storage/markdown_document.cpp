#include "storage/markdown_document.hpp"

#include "domain/errors.hpp"

#include <serde/sparcli_serde.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>
#include <unistd.h>

namespace mdtask {

namespace {

namespace serde = sparcli::serde;

/** Deep-copies a borrowed value view into an independently owned value. */
serde::Value clone_view(serde::View view) {
    return serde::Value::adopt(sc_value_clone(view.get()));
}

/** Reads a whole file into a string; throws StorageError on failure. */
std::string read_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if(!stream) {
        throw StorageError("Cannot read file: " + path.string());
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

/** Strips trailing newlines so we control the exact block separators. */
std::string_view without_trailing_newlines(std::string_view text) {
    const auto end = text.find_last_not_of('\n');
    if(end == std::string_view::npos) {
        return {};
    }
    return text.substr(0, end + 1);
}

}  // namespace

std::optional<MarkdownDocument> read_markdown(
    const std::filesystem::path& path
) {
    std::error_code error;
    if(!std::filesystem::exists(path, error)) {
        return std::nullopt;
    }

    const std::string text = read_file(path);
    serde::Markdown markdown = serde::Markdown::parse(text);

    serde::Value frontmatter = markdown.frontmatter()
        ? clone_view(*markdown.frontmatter())
        : serde::Value::object();

    return MarkdownDocument{
        .frontmatter = std::move(frontmatter),
        .body        = std::string(markdown.body()),
    };
}

void write_markdown(
    const std::filesystem::path& path,
    const sparcli::serde::Value& frontmatter,
    std::string_view body
) {
    const auto parent = path.parent_path();
    if(!parent.empty()) {
        std::error_code error;
        std::filesystem::create_directories(parent, error);
        if(error) {
            throw StorageError(
                "Cannot create directory: " + parent.string()
            );
        }
    }

    // Compose "---\n<yaml>\n---\n\n<body>\n" with deterministic separators.
    const std::string yaml = serde::yaml::write(frontmatter);
    std::string content = "---\n";
    content += without_trailing_newlines(yaml);
    content += "\n---\n";
    const std::string_view trimmed_body = without_trailing_newlines(body);
    if(!trimmed_body.empty()) {
        content += "\n";
        content += trimmed_body;
    }
    content += "\n";

    // Write to a sibling temp file, then rename atomically over the target.
    std::filesystem::path temp = path;
    temp += ".tmp." + std::to_string(::getpid());
    {
        std::ofstream stream(temp, std::ios::binary | std::ios::trunc);
        if(!stream) {
            throw StorageError("Cannot write file: " + temp.string());
        }
        stream << content;
        if(!stream) {
            throw StorageError("Cannot write file: " + temp.string());
        }
    }

    std::error_code error;
    std::filesystem::rename(temp, path, error);
    if(error) {
        std::filesystem::remove(temp, error);
        throw StorageError("Cannot replace file: " + path.string());
    }
}

std::vector<std::filesystem::path> list_markdown_files(
    const std::filesystem::path& dir
) {
    std::vector<std::filesystem::path> files;

    std::error_code error;
    if(!std::filesystem::is_directory(dir, error)) {
        return files;
    }

    for(const auto& entry : std::filesystem::directory_iterator(dir, error)) {
        if(entry.is_regular_file() && entry.path().extension() == ".md") {
            files.push_back(entry.path());
        }
    }
    std::ranges::sort(files);
    return files;
}

std::vector<std::filesystem::path> list_markdown_files_recursive(
    const std::filesystem::path& dir
) {
    std::vector<std::filesystem::path> files;

    std::error_code error;
    if(!std::filesystem::is_directory(dir, error)) {
        return files;
    }

    for(const auto& entry :
        std::filesystem::recursive_directory_iterator(dir, error)) {
        if(entry.is_regular_file() && entry.path().extension() == ".md") {
            files.push_back(entry.path());
        }
    }
    std::ranges::sort(files);
    return files;
}

}  // namespace mdtask
