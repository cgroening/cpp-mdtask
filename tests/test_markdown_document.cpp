#include "storage/markdown_document.hpp"

#include "check.hpp"
#include "test_suite.hpp"

#include <serde/sparcli_serde.hpp>

#include <filesystem>
#include <fstream>
#include <string>

using namespace mdtask;

namespace {

namespace fs = std::filesystem;
namespace serde = sparcli::serde;

fs::path test_dir() {
    return fs::temp_directory_path() / "mdtask-md-test";
}

void write_raw(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::trunc);
    stream << content;
}

}  // namespace

void run_markdown_document_tests() {
    const fs::path dir = test_dir();
    std::error_code ec;
    fs::remove_all(dir, ec);

    // A written front matter + body round-trips back to the same values.
    {
        serde::Value frontmatter = serde::Value::object();
        frontmatter.set("id", serde::Value::string("abc123"));
        frontmatter.set("done", serde::Value::boolean(true));

        const fs::path path = dir / "round-trip.md";
        write_markdown(path, frontmatter, "# Title\n\nBody text");

        const auto document = read_markdown(path);
        CHECK(document.has_value());
        const serde::View view = document->frontmatter.view();
        CHECK(view.get("id").as_string() == "abc123");
        CHECK(view.get("done").as_bool() == true);
        CHECK(document->body.find("# Title") != std::string::npos);
        CHECK(document->body.find("Body text") != std::string::npos);
    }

    // A missing file reads as std::nullopt, not an error.
    {
        const auto document = read_markdown(dir / "does-not-exist.md");
        CHECK(!document.has_value());
    }

    // A file without front matter yields an empty object and a full body.
    {
        const fs::path path = dir / "no-frontmatter.md";
        write_raw(path, "# Just a body\n\nNo front matter here\n");

        const auto document = read_markdown(path);
        CHECK(document.has_value());
        CHECK(!document->frontmatter.view().get("id"));
        CHECK(document->body.find("Just a body") != std::string::npos);
    }

    fs::remove_all(dir, ec);
}
