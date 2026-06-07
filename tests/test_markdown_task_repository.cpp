#include "domain/task.hpp"
#include "storage/markdown_task_repository.hpp"

#include "check.hpp"
#include "test_suite.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
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
    const fs::path notes_dir = dir / "notes";
    const fs::path archive_dir = dir / "archive";
    const fs::path notes_archive_dir = dir / "notes-archive";

    // save then find_by_id round-trips every field through the file.
    {
        MarkdownTaskRepository repository(tasks_dir, notes_dir, archive_dir, notes_archive_dir);
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
        MarkdownTaskRepository repository(tasks_dir, notes_dir, archive_dir, notes_archive_dir);
        Task moved = sample_task();
        moved.due = ymd(2026, 6, 10);
        repository.update(moved);

        CHECK(!any_file_named(tasks_dir, "2026-06-05--pay-the-invoice.md"));
        CHECK(any_file_named(tasks_dir, "2026-06-10--pay-the-invoice.md"));
        CHECK(repository.find_all().size() == 1);
    }

    // Archiving moves the file under archive/<year>/<month>/ by completion.
    {
        MarkdownTaskRepository repository(tasks_dir, notes_dir, archive_dir, notes_archive_dir);
        Task done = sample_task();
        done.due = ymd(2026, 6, 10);
        done.status = Status::DONE;
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

    // status round-trips through the file, and a legacy `done:`-only file (no
    // `status:`) is read as DONE.
    {
        fs::remove_all(dir, ec);
        MarkdownTaskRepository repository(tasks_dir, notes_dir, archive_dir, notes_archive_dir);

        Task in_progress = sample_task();
        in_progress.status = Status::IN_PROGRESS;
        repository.save(in_progress);
        CHECK(repository.find_by_id("id000000")->status
              == Status::IN_PROGRESS);

        fs::create_directories(tasks_dir, ec);
        std::ofstream(tasks_dir / "legacy.md")
            << "---\nid: legacy1\ntitle: Legacy\ndone: true\n---\n# Legacy\n";
        const auto legacy = repository.find_by_id("legacy1");
        CHECK(legacy.has_value());
        CHECK(legacy->status == Status::DONE);
    }

    // order round-trips; absent in the file means nullopt.
    {
        fs::remove_all(dir, ec);
        MarkdownTaskRepository repository(tasks_dir, notes_dir, archive_dir, notes_archive_dir);

        Task task = sample_task();
        CHECK(!task.order.has_value());
        repository.save(task);
        CHECK(!repository.find_by_id("id000000")->order.has_value());

        task.order = 5;
        repository.update(task);
        CHECK(repository.find_by_id("id000000")->order == 5);
    }

    // status: cancelled and paused round-trip.
    {
        fs::remove_all(dir, ec);
        MarkdownTaskRepository repository(tasks_dir, notes_dir, archive_dir, notes_archive_dir);
        Task task = sample_task();
        task.status = Status::CANCELLED;
        repository.save(task);
        CHECK(repository.find_by_id("id000000")->status == Status::CANCELLED);

        task.status = Status::PAUSED;
        repository.update(task);
        CHECK(repository.find_by_id("id000000")->status == Status::PAUSED);
    }

    // remove deletes an active file and an archived file.
    {
        fs::remove_all(dir, ec);
        MarkdownTaskRepository repository(tasks_dir, notes_dir, archive_dir, notes_archive_dir);

        Task active = sample_task();
        repository.save(active);
        repository.remove(active);
        CHECK(repository.find_all().empty());

        Task done = sample_task();
        done.status = Status::DONE;
        done.completed_at = "2026-06-06T09:00:00";
        repository.save(done);
        repository.archive(done);
        CHECK(repository.find_archived().size() == 1);
        repository.remove(done);
        CHECK(repository.find_archived().empty());
    }

    // Notes route to the notes tree; toggling the note flag moves the file.
    {
        fs::remove_all(dir, ec);
        MarkdownTaskRepository repository(
            tasks_dir, notes_dir, archive_dir, notes_archive_dir
        );

        Task note = sample_task();
        note.due = std::nullopt;
        note.note = true;
        repository.save(note);
        CHECK(repository.find_all().empty());          // not in the task list
        CHECK(repository.find_notes().size() == 1);
        CHECK(any_file_named(notes_dir, "nodate--pay-the-invoice.md"));

        // Archiving a note uses the notes archive tree.
        repository.archive(note);
        CHECK(repository.find_notes().empty());
        const auto archived = repository.find_archived();
        CHECK(archived.size() == 1);
        CHECK(archived[0].note);
        repository.unarchive(archived[0]);
        CHECK(repository.find_notes().size() == 1);

        // Converting the note to a task moves it into the tasks dir.
        Task as_task = *repository.find_by_id("id000000");
        as_task.note = false;
        repository.update(as_task);
        CHECK(repository.find_notes().empty());
        CHECK(repository.find_all().size() == 1);
        CHECK(!any_file_named(notes_dir, "nodate--pay-the-invoice.md"));
    }

    fs::remove_all(dir, ec);
}
