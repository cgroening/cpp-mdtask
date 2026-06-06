#include "domain/errors.hpp"
#include "service/task_service.hpp"
#include "storage/in_memory_task_repository.hpp"
#include "util/date.hpp"

#include "check.hpp"
#include "test_suite.hpp"

#include <chrono>

using namespace mdtask;

namespace {

std::chrono::year_month_day ymd(int year, unsigned month, unsigned day) {
    return std::chrono::year_month_day{
        std::chrono::year{year},
        std::chrono::month{month},
        std::chrono::day{day},
    };
}

}  // namespace

// The service is tested against the in-memory fake, so no filesystem is
// involved - this is the payoff of depending on the repository interface.
void run_service_tests() {
    // add_task stores a task and returns it with a generated id.
    {
        InMemoryTaskRepository repository;
        TaskService service(repository);

        const auto task = service.add_task({.title = "Write tests"});
        CHECK(task.has_value());
        CHECK(task->title == "Write tests");
        CHECK(!task->id.empty());
        CHECK(!task->done);
        CHECK(service.all_tasks().size() == 1);
    }

    // add_task trims surrounding whitespace and persists the optional fields.
    {
        InMemoryTaskRepository repository;
        TaskService service(repository);

        const auto task = service.add_task({
            .title       = "   padded   ",
            .description = "body",
            .due         = ymd(2026, 6, 5),
            .priority    = Priority::HIGH,
        });
        CHECK(task.has_value());
        CHECK(task->title == "padded");
        CHECK(task->description == "body");
        CHECK(task->due == ymd(2026, 6, 5));
        CHECK(task->priority == Priority::HIGH);
    }

    // add_task rejects an empty or whitespace-only title.
    {
        InMemoryTaskRepository repository;
        TaskService service(repository);

        const auto task = service.add_task({.title = "   "});
        CHECK(!task.has_value());
        CHECK(task.error().code == ErrorCode::VALIDATION_FAILED);
        CHECK(service.all_tasks().empty());
    }

    // toggle_done flips state and records, then clears, the completion stamp.
    {
        InMemoryTaskRepository repository;
        TaskService service(repository);

        const auto created = service.add_task({.title = "Finish me"});
        CHECK(created.has_value());

        const auto done = service.toggle_done(created->id);
        CHECK(done.has_value());
        CHECK(done->done);
        CHECK(done->completed_at.has_value());

        const auto reopened = service.toggle_done(created->id);
        CHECK(reopened.has_value());
        CHECK(!reopened->done);
        CHECK(!reopened->completed_at.has_value());
    }

    // toggle_done on an unknown id reports a not-found error.
    {
        InMemoryTaskRepository repository;
        TaskService service(repository);

        const auto done = service.toggle_done("nope");
        CHECK(!done.has_value());
        CHECK(done.error().code == ErrorCode::NOT_FOUND);
    }

    // shift_due moves an existing due date by the given number of days.
    {
        InMemoryTaskRepository repository;
        TaskService service(repository);

        const auto created =
            service.add_task({.title = "Dated", .due = ymd(2026, 6, 5)});
        CHECK(created.has_value());

        const auto shifted = service.shift_due(created->id, 2);
        CHECK(shifted.has_value());
        CHECK(shifted->due == ymd(2026, 6, 7));
    }

    // shift_due puts a dateless task onto the calendar and clears someday.
    {
        InMemoryTaskRepository repository;
        TaskService service(repository);

        const auto created =
            service.add_task({.title = "Someday", .someday = true});
        CHECK(created.has_value());

        const auto shifted = service.shift_due(created->id, 1);
        CHECK(shifted.has_value());
        CHECK(shifted->due == shift_days(today(), 1));
        CHECK(!shifted->someday);
    }

    // set_priority updates only the priority.
    {
        InMemoryTaskRepository repository;
        TaskService service(repository);

        const auto created = service.add_task({.title = "Prioritize"});
        CHECK(created.has_value());

        const auto changed =
            service.set_priority(created->id, Priority::MEDIUM);
        CHECK(changed.has_value());
        CHECK(changed->priority == Priority::MEDIUM);
    }

    // archive_task removes the task from the active set.
    {
        InMemoryTaskRepository repository;
        TaskService service(repository);

        const auto created = service.add_task({.title = "Archive me"});
        CHECK(created.has_value());
        CHECK(service.archive_task(created->id).has_value());
        CHECK(service.all_tasks().empty());
    }

    // open_tasks returns only tasks that are not done yet.
    {
        InMemoryTaskRepository repository;
        TaskService service(repository);

        const auto first  = service.add_task({.title = "First"});
        const auto second = service.add_task({.title = "Second"});
        CHECK(first.has_value());
        CHECK(second.has_value());

        CHECK(service.toggle_done(first->id).has_value());

        const auto open = service.open_tasks();
        CHECK(open.size() == 1);
        CHECK(open.front().id == second->id);
    }

    // update_task rejects an empty title.
    {
        InMemoryTaskRepository repository;
        TaskService service(repository);

        const auto created = service.add_task({.title = "Keep"});
        CHECK(created.has_value());

        Task blank = *created;
        blank.title = "   ";
        const auto updated = service.update_task(blank);
        CHECK(!updated.has_value());
        CHECK(updated.error().code == ErrorCode::VALIDATION_FAILED);
    }
}
