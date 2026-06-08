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
        CHECK(task->status == Status::OPEN);
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
        CHECK(done->status == Status::DONE);
        CHECK(done->completed_at.has_value());

        const auto reopened = service.toggle_done(created->id);
        CHECK(reopened.has_value());
        CHECK(reopened->status == Status::OPEN);
        CHECK(!reopened->completed_at.has_value());
    }

    // set_status walks the lifecycle and manages completed_at.
    {
        InMemoryTaskRepository repository;
        TaskService service(repository);

        const auto created = service.add_task({.title = "Lifecycle"});
        CHECK(created.has_value());

        const auto progress =
            service.set_status(created->id, Status::IN_PROGRESS);
        CHECK(progress.has_value());
        CHECK(progress->status == Status::IN_PROGRESS);
        CHECK(!progress->completed_at.has_value());

        const auto done = service.set_status(created->id, Status::DONE);
        CHECK(done.has_value());
        CHECK(done->completed_at.has_value());

        const auto open = service.set_status(created->id, Status::OPEN);
        CHECK(open.has_value());
        CHECK(!open->completed_at.has_value());

        CHECK(!service.set_status("nope", Status::DONE).has_value());
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

    // shift_due puts a dateless task onto today (first press) and clears
    // someday, regardless of the shift direction.
    {
        InMemoryTaskRepository repository;
        TaskService service(repository);

        const auto created =
            service.add_task({.title = "Someday", .someday = true});
        CHECK(created.has_value());

        const auto shifted = service.shift_due(created->id, 1);
        CHECK(shifted.has_value());
        CHECK(shifted->due == today());
        CHECK(!shifted->someday);

        // A dated task then shifts normally by the given number of days.
        const auto again = service.shift_due(created->id, 1);
        CHECK(again.has_value());
        CHECK(again->due == shift_days(today(), 1));
    }

    // set_due sets a specific date, clearing someday; nullopt clears the date.
    {
        InMemoryTaskRepository repository;
        TaskService service(repository);

        const auto created =
            service.add_task({.title = "Pick", .someday = true});
        CHECK(created.has_value());

        const auto dated = service.set_due(created->id, ymd(2026, 7, 1));
        CHECK(dated.has_value());
        CHECK(dated->due == ymd(2026, 7, 1));
        CHECK(!dated->someday);

        const auto cleared = service.set_due(created->id, std::nullopt);
        CHECK(cleared.has_value());
        CHECK(!cleared->due.has_value());
    }

    // set_due on an unknown id reports NOT_FOUND.
    {
        InMemoryTaskRepository repository;
        TaskService service(repository);

        const auto missing = service.set_due("nope", ymd(2026, 7, 1));
        CHECK(!missing.has_value());
        CHECK(missing.error().code == ErrorCode::NOT_FOUND);
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

    // archive_task moves the task from the active set into the archive.
    {
        InMemoryTaskRepository repository;
        TaskService service(repository);

        const auto created = service.add_task({.title = "Archive me"});
        CHECK(created.has_value());
        CHECK(service.archived_tasks().empty());
        CHECK(service.archive_task(created->id).has_value());
        CHECK(service.all_tasks().empty());
        CHECK(service.archived_tasks().size() == 1);
        CHECK(service.archived_tasks()[0].id == created->id);

        // restore_task moves it back into the active set.
        CHECK(service.restore_task(created->id).has_value());
        CHECK(service.archived_tasks().empty());
        CHECK(service.all_tasks().size() == 1);
        CHECK(service.all_tasks()[0].id == created->id);

        // Restoring an unknown id fails.
        CHECK(!service.restore_task("missing").has_value());
    }

    // New tasks get an increasing order; move_task reorders within a section.
    {
        InMemoryTaskRepository repository;
        TaskService service(repository);

        const auto a = service.add_task({.title = "A"});
        const auto b = service.add_task({.title = "B"});
        const auto c = service.add_task({.title = "C"});
        CHECK(a->order.value() < b->order.value());
        CHECK(b->order.value() < c->order.value());

        const auto order_of = [&](const std::string& id) {
            for(const auto& task : service.all_tasks()) {
                if(task.id == id) {
                    return task.order.value_or(-1);
                }
            }
            return -1;
        };

        // Move C to the top of the (Inbox) section.
        CHECK(service.move_task(c->id, MoveDir::TOP).has_value());
        CHECK(order_of(c->id) < order_of(a->id));
        CHECK(order_of(a->id) < order_of(b->id));

        // Move C down one: now A, C, B.
        CHECK(service.move_task(c->id, MoveDir::DOWN).has_value());
        CHECK(order_of(a->id) < order_of(c->id));
        CHECK(order_of(c->id) < order_of(b->id));
    }

    // Changing the date and reopening both re-append at the end of the section.
    {
        InMemoryTaskRepository repository;
        TaskService service(repository);

        const auto a = service.add_task({.title = "A"});   // order 0
        const auto b = service.add_task({.title = "B"});   // order 1

        const auto shifted = service.shift_due(a->id, 1);
        CHECK(shifted.has_value());
        CHECK(shifted->order == 2);   // append: max active order (1) + 1

        CHECK(service.set_status(b->id, Status::DONE).has_value());
        const auto reopened = service.set_status(b->id, Status::OPEN);
        CHECK(reopened.has_value());
        CHECK(reopened->order == 3);  // append over A's new order (2)
    }

    // Cancelling is terminal: it records completed_at and leaves the open set.
    {
        InMemoryTaskRepository repository;
        TaskService service(repository);

        const auto created = service.add_task({.title = "Drop me"});
        CHECK(created.has_value());

        const auto cancelled =
            service.set_status(created->id, Status::CANCELLED);
        CHECK(cancelled.has_value());
        CHECK(cancelled->status == Status::CANCELLED);
        CHECK(cancelled->completed_at.has_value());
        CHECK(service.open_tasks().empty());
    }

    // Pausing is active: no completion timestamp, still in the open set.
    {
        InMemoryTaskRepository repository;
        TaskService service(repository);

        const auto created = service.add_task({.title = "Hold me"});
        CHECK(created.has_value());

        const auto paused = service.set_status(created->id, Status::PAUSED);
        CHECK(paused.has_value());
        CHECK(paused->status == Status::PAUSED);
        CHECK(!paused->completed_at.has_value());
        CHECK(service.open_tasks().size() == 1);
    }

    // delete_task removes active and archived tasks; unknown ids error.
    {
        InMemoryTaskRepository repository;
        TaskService service(repository);

        const auto active = service.add_task({.title = "Active"});
        CHECK(service.delete_task(active->id).has_value());
        CHECK(service.all_tasks().empty());

        const auto archived = service.add_task({.title = "Archived"});
        CHECK(service.archive_task(archived->id).has_value());
        CHECK(service.archived_tasks().size() == 1);
        CHECK(service.delete_task(archived->id).has_value());
        CHECK(service.archived_tasks().empty());

        CHECK(!service.delete_task("nope").has_value());
    }

    // Notes live in their own list with their own order; converting strips the
    // task-only fields and restores defaults the other way.
    {
        InMemoryTaskRepository repository;
        TaskService service(repository);

        const auto note = service.add_task({.title = "Idea", .note = true});
        CHECK(note.has_value());
        CHECK(note->note);
        CHECK(service.notes().size() == 1);
        CHECK(service.all_tasks().empty());          // notes excluded from tasks
        CHECK(note->order.has_value());

        // Convert the note to a task: defaults filled in, leaves the notes list.
        Task as_task = *note;
        as_task.note = false;
        as_task.priority = Priority::HIGH;
        CHECK(service.update_task(as_task).has_value());
        CHECK(service.notes().empty());
        CHECK(service.all_tasks().size() == 1);

        // Convert a task to a note: due/priority/status are dropped.
        const auto task = service.add_task(
            {.title = "Buy milk", .due = ymd(2026, 6, 10),
             .priority = Priority::HIGH}
        );
        Task as_note = *task;
        as_note.note = true;
        const auto converted = service.update_task(as_note);
        CHECK(converted.has_value());
        CHECK(converted->note);
        CHECK(!converted->due.has_value());
        CHECK(converted->priority == Priority::NONE);
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
