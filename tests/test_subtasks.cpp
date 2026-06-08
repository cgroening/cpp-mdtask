#include "domain/subtasks.hpp"

#include "check.hpp"
#include "test_suite.hpp"

using namespace mdtask;

void run_subtasks_tests() {
    // An empty body has no checkboxes.
    {
        const auto p = count_subtasks("");
        CHECK(p.total == 0);
        CHECK(p.done == 0);
    }

    // Prose without checkboxes counts nothing.
    {
        const auto p = count_subtasks("Just a description.\nSecond line.");
        CHECK(p.total == 0);
        CHECK(p.done == 0);
    }

    // A mix of open and done boxes across the three bullet styles.
    {
        const auto p = count_subtasks(
            "- [ ] open dash\n"
            "* [x] done star\n"
            "+ [X] done plus\n"
        );
        CHECK(p.total == 3);
        CHECK(p.done == 2);
    }

    // Leading indentation is allowed.
    {
        const auto p = count_subtasks("    - [ ] indented\n\t- [x] tabbed");
        CHECK(p.total == 2);
        CHECK(p.done == 1);
    }

    // A box that ends the line (no trailing text) still counts.
    {
        const auto p = count_subtasks("- [ ]");
        CHECK(p.total == 1);
        CHECK(p.done == 0);
    }

    // Non-checkbox list items and malformed boxes are ignored.
    {
        const auto p = count_subtasks(
            "- plain item\n"
            "-[ ] no space after bullet\n"
            "- [] empty brackets\n"
            "- [y] wrong mark\n"
            "- [ ]x glued text\n"
            "- [x] real one\n"
        );
        CHECK(p.total == 1);
        CHECK(p.done == 1);
    }

    // CRLF line endings are handled.
    {
        const auto p = count_subtasks("- [ ] one\r\n- [x] two\r\n");
        CHECK(p.total == 2);
        CHECK(p.done == 1);
    }
}
