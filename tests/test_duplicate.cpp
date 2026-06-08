#include "domain/duplicate.hpp"

#include "check.hpp"
#include "test_suite.hpp"

#include <string>
#include <vector>

using namespace mdtask;

void run_duplicate_tests() {
    // No existing copy yields the plain "(copy)" suffix.
    {
        CHECK(next_copy_title("Pay invoice", {}) == "Pay invoice (copy)");
    }

    // An existing "(copy)" bumps to "(copy 2)", then "(copy 3)".
    {
        const std::vector<std::string> existing = {"X", "X (copy)"};
        CHECK(next_copy_title("X", existing) == "X (copy 2)");
        const std::vector<std::string> more = {"X", "X (copy)", "X (copy 2)"};
        CHECK(next_copy_title("X", more) == "X (copy 3)");
    }

    // Duplicating an item that is already a copy strips the suffix first.
    {
        const std::vector<std::string> existing = {"X (copy)"};
        CHECK(next_copy_title("X (copy)", existing) == "X (copy 2)");
        CHECK(next_copy_title("X (copy 3)", {}) == "X (copy)");
    }

    // Gaps in the numbering are filled (first free wins).
    {
        const std::vector<std::string> existing = {"X (copy)", "X (copy 3)"};
        CHECK(next_copy_title("X", existing) == "X (copy 2)");
    }

    // A trailing "(copy ...)" that is not a number is left intact as the base.
    {
        CHECK(next_copy_title("Report (copy draft)", {})
              == "Report (copy draft) (copy)");
    }
}
