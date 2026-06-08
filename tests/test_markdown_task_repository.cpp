#include "domain/recurrence.hpp"
#include "domain/task.hpp"
#include "service/task_service.hpp"
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

    // A recurrence rule round-trips through the file, including the basis; a
    // file without the keys loads as a non-recurring task.
    {
        fs::remove_all(dir, ec);
        MarkdownTaskRepository repository(
            tasks_dir, notes_dir, archive_dir, notes_archive_dir
        );

        Task task = sample_task();
        task.recurrence = parse_recurrence("mon,wed,fri", "completion");
        repository.save(task);

        const auto loaded = repository.find_by_id("id000000");
        CHECK(loaded.has_value());
        CHECK(loaded->recurrence.has_value());
        CHECK(loaded->recurrence->weekdays.size() == 3);
        CHECK(loaded->recurrence->basis == RecurBasis::COMPLETION);
        CHECK(format_recurrence(*loaded->recurrence) == "mon,wed,fri");

        // The due basis is the default and omits repeat_from, but still round-
        // trips the interval rule.
        Task interval = sample_task();
        interval.recurrence = parse_recurrence("every 2 weeks", "");
        repository.update(interval);
        const auto reloaded = repository.find_by_id("id000000");
        CHECK(reloaded->recurrence.has_value());
        CHECK(reloaded->recurrence->basis == RecurBasis::DUE);
        CHECK(format_recurrence(*reloaded->recurrence) == "every 2 weeks");

        // A task with no repeat key loads as non-recurring.
        Task plain = sample_task();
        plain.recurrence = std::nullopt;
        repository.update(plain);
        CHECK(!repository.find_by_id("id000000")->recurrence.has_value());
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

    // A file with malformed front matter is skipped (not shown as a ghost
    // task), recorded as a load warning, and does not abort the whole load.
    {
        const fs::path bad_dir = dir / "bad-tasks";
        fs::remove_all(bad_dir, ec);
        MarkdownTaskRepository repository(
            bad_dir, notes_dir, archive_dir, notes_archive_dir
        );
        repository.save(sample_task());   // one valid task
        CHECK(repository.find_all().size() == 1);
        CHECK(repository.load_warnings().empty());

        // `{a b}` is a flow mapping without a colon: invalid YAML.
        std::ofstream(bad_dir / "2026-01-01--broken.md")
            << "---\ntitle: Broken\nbad: {a b}\n---\n\n# Broken\n\nbody\n";

        const auto loaded = repository.find_all();
        CHECK(loaded.size() == 1);                    // the valid one survives
        CHECK(loaded[0].id == "id000000");
        CHECK(repository.load_warnings().size() == 1);  // the broken one warned

        // The warning names the file and carries sparcli's concrete reason,
        // including the 1-based source line of the YAML error.
        const std::string warning = repository.load_warnings()[0];
        CHECK(warning.find("2026-01-01--broken.md") != std::string::npos);
        CHECK(warning.find("line ") != std::string::npos);
    }

    // file_path locates the backing file; reload_task re-reads an externally
    // edited file and renames it when its due date changed.
    {
        const fs::path edit_dir = dir / "edit-tasks";
        fs::remove_all(edit_dir, ec);
        MarkdownTaskRepository repository(
            edit_dir, notes_dir, archive_dir, notes_archive_dir
        );
        TaskService service(repository);
        repository.save(sample_task());   // due 2026-06-05

        const auto path = repository.file_path("id000000");
        CHECK(path.has_value());
        CHECK(path->filename() == "2026-06-05--pay-the-invoice.md");

        // Simulate an external edit that moves the due date to 2026-06-10.
        std::ofstream(*path, std::ios::trunc)
            << "---\nid: id000000\ntitle: Pay the invoice\n"
               "due: 2026-06-10\npriority: high\ncreated: 2026-06-01\n"
               "---\n\n# Pay the invoice\n\nRechnung 4711\n";

        const auto reloaded = service.reload_task("id000000");
        CHECK(reloaded.has_value());
        CHECK(reloaded->due == ymd(2026, 6, 10));
        CHECK(any_file_named(edit_dir, "2026-06-10--pay-the-invoice.md"));
        CHECK(!any_file_named(edit_dir, "2026-06-05--pay-the-invoice.md"));

        // An unknown id cannot be reloaded.
        const auto missing = service.reload_task("nope");
        CHECK(!missing.has_value());
    }

    fs::remove_all(dir, ec);
}
