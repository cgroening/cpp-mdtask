#include "tui/finder_actions.hpp"

#include "domain/task.hpp"

#include "check.hpp"
#include "test_suite.hpp"

#include <optional>
#include <string>
#include <vector>

using namespace mdtask;

void run_finder_actions_tests() {
    // `p` toggles in progress <-> paused; any other status starts in progress.
    {
        CHECK(toggle_progress(Status::IN_PROGRESS) == Status::PAUSED);
        CHECK(toggle_progress(Status::PAUSED) == Status::IN_PROGRESS);
        CHECK(toggle_progress(Status::OPEN) == Status::IN_PROGRESS);
        CHECK(toggle_progress(Status::DONE) == Status::IN_PROGRESS);
        CHECK(toggle_progress(Status::CANCELLED) == Status::IN_PROGRESS);
    }

    // `d` cycles done -> cancelled -> open; any other status jumps to done.
    {
        CHECK(cycle_done(Status::OPEN) == Status::DONE);
        CHECK(cycle_done(Status::DONE) == Status::CANCELLED);
        CHECK(cycle_done(Status::CANCELLED) == Status::OPEN);
        CHECK(cycle_done(Status::IN_PROGRESS) == Status::DONE);
        CHECK(cycle_done(Status::PAUSED) == Status::DONE);
    }

    // Which actions fan out across a selection.
    {
        CHECK(action_is_bulk(ACT_TOGGLE_DONE));
        CHECK(action_is_bulk(ACT_CYCLE_STATUS));
        CHECK(action_is_bulk(ACT_ARCHIVE));
        CHECK(action_is_bulk(ACT_DELETE));
        CHECK(action_is_bulk(ACT_SHIFT_PLUS));
        CHECK(action_is_bulk(ACT_PICK_DATE));
        CHECK(!action_is_bulk(ACT_MOVE_UP));
        CHECK(!action_is_bulk(ACT_NEW));
        CHECK(!action_is_bulk(ACT_NONE));
    }

    // No selection: the action targets the cursor task only.
    {
        const auto targets = action_targets(ACT_ARCHIVE, "cursor", {});
        CHECK(targets.size() == 1);
        CHECK(targets[0] == "cursor");
    }

    // A bulk action with a selection targets the whole selection.
    {
        const std::vector<std::string> sel = {"a", "b", "c"};
        const auto targets = action_targets(ACT_TOGGLE_DONE, "cursor", sel);
        CHECK(targets.size() == 3);
        CHECK(targets[0] == "a");
        CHECK(targets[2] == "c");
    }

    // A non-bulk action ignores the selection and acts on the cursor only.
    {
        const std::vector<std::string> sel = {"a", "b"};
        const auto targets = action_targets(ACT_MOVE_UP, "cursor", sel);
        CHECK(targets.size() == 1);
        CHECK(targets[0] == "cursor");
    }

    // focus_after_delete prefers the next task in the same section.
    {
        const std::vector<std::optional<std::string>> rows = {
            std::nullopt,   // section header
            std::string("t1"),
            std::string("t2"),
            std::string("t3"),
        };
        const auto next = focus_after_delete(rows, 2);   // delete t2
        CHECK(next.has_value());
        CHECK(*next == "t3");
    }

    // Deleting the last task in a section falls back to the one above.
    {
        const std::vector<std::optional<std::string>> rows = {
            std::nullopt,
            std::string("t1"),
            std::string("t2"),
        };
        const auto next = focus_after_delete(rows, 2);   // delete t2
        CHECK(next.has_value());
        CHECK(*next == "t1");
    }

    // Deleting the sole task of the first section jumps to the next section.
    {
        const std::vector<std::optional<std::string>> rows = {
            std::nullopt,
            std::string("only"),
            std::nullopt,
            std::string("other"),
        };
        const auto next = focus_after_delete(rows, 1);
        CHECK(next.has_value());
        CHECK(*next == "other");
    }

    // Deleting the only task anywhere leaves nothing to focus.
    {
        const std::vector<std::optional<std::string>> rows = {
            std::nullopt,
            std::string("solo"),
        };
        const auto next = focus_after_delete(rows, 1);
        CHECK(!next.has_value());
    }
}
