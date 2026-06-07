#include "domain/task.hpp"
#include "storage/markdown_task_repository.hpp"

#include "check.hpp"
#include "test_suite.hpp"

#include <chrono>
#include <filesystem>
#include <string>

using namespace mdtask;

namespace {

namespace fs = std::filesystem;

std::chrono::year_month_day ymd(int year, unsigned month, unsigned day) {
    return std::chrono::year_month_day{
        std::chrono::year{year},
        std::chrono::month{month},
        std::chrono::day{day},
    };
}

fs::path test_dir() {
    return fs::temp_directory_path() / "mdtask-repo-test";
}

Task sample_task() {
    Task task;
    task.id = "id000000";
    task.title = "Pay the invoice";
    task.description = "Rechnung 4711";
    task.due = ymd(2026, 6, 5);
    task.priority = Priority::HIGH;
    task.created = ymd(2026, 6, 1);
    return task;
}

bool any_file_named(const fs::path& dir, const std::string& name) {
    return fs::exists(dir / name);
}

}  // namespace

void run_markdown_task_repository_tests() {
    // slugify normalizes case, German letters and separators.
    {
        CHECK(slugify("Ich bin der Titel") == "ich-bin-der-titel");
        CHECK(slugify("Über Äpfel & Öl") == "ueber-aepfel-oel");
        CHECK(slugify("  spaced  out  ") == "spaced-out");
        CHECK(slugify("???") == "untitled");
    }

    // task_filename uses the ISO due date, or the nodate prefix.
    {
        Task dated = sample_task();
        CHECK(task_filename(dated) == "2026-06-05--pay-the-invoice.md");

        Task undated = sample_task();
        undated.due = std::nullopt;
        CHECK(task_filename(undated) == "nodate--pay-the-invoice.md");
    }

    const fs::path dir = test_dir();
    std::error_code ec;
    fs::remove_all(dir, ec);
    const fs::path tasks_dir = dir / "tasks";
    const fs::path archive_dir = dir / "archive";

    // save then find_by_id round-trips every field through the file.
    {
        MarkdownTaskRepository repository(tasks_dir, archive_dir);
        repository.save(sample_task());

        CHECK(any_file_named(tasks_dir, "2026-06-05--pay-the-invoice.md"));
        CHECK(repository.find_all().size() == 1);

        const auto found = repository.find_by_id("id000000");
        CHECK(found.has_value());
        CHECK(found->title == "Pay the invoice");
        CHECK(found->description == "Rechnung 4711");
        CHECK(found->due == ymd(2026, 6, 5));
        CHECK(found->priority == Priority::HIGH);
        CHECK(found->created == ymd(2026, 6, 1));
    }

    // Changing the due date renames the file rather than duplicating it.
    {
        MarkdownTaskRepository repository(tasks_dir, archive_dir);
        Task moved = sample_task();
        moved.due = ymd(2026, 6, 10);
        repository.update(moved);

        CHECK(!any_file_named(tasks_dir, "2026-06-05--pay-the-invoice.md"));
        CHECK(any_file_named(tasks_dir, "2026-06-10--pay-the-invoice.md"));
        CHECK(repository.find_all().size() == 1);
    }

    // Archiving moves the file under archive/<year>/<month>/ by completion.
    {
        MarkdownTaskRepository repository(tasks_dir, archive_dir);
        Task done = sample_task();
        done.due = ymd(2026, 6, 10);
        done.done = true;
        done.completed_at = "2026-06-06T09:00:00";
        repository.archive(done);

        CHECK(repository.find_all().empty());
        CHECK(any_file_named(
            archive_dir / "2026" / "06", "2026-06-10--pay-the-invoice.md"
        ));

        // find_archived reads the nested archive tree back.
        const auto archived = repository.find_archived();
        CHECK(archived.size() == 1);
        CHECK(archived[0].id == "id000000");
        CHECK(archived[0].title == "Pay the invoice");

        // unarchive moves the file back into the active directory.
        repository.unarchive(archived[0]);
        CHECK(repository.find_archived().empty());
        CHECK(repository.find_all().size() == 1);
        CHECK(any_file_named(tasks_dir, "2026-06-10--pay-the-invoice.md"));
        CHECK(!any_file_named(
            archive_dir / "2026" / "06", "2026-06-10--pay-the-invoice.md"
        ));
    }

    fs::remove_all(dir, ec);
}
