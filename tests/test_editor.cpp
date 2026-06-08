#include "check.hpp"
#include "test_suite.hpp"

#include <sparcli.hpp>

#include <cstdlib>

void run_editor_tests() {
    // With no terminal available (SPARCLI_NO_TTY), edit_file refuses to spawn
    // and reports -1 - exactly the case the finder treats as "skipped + warn".
    {
        setenv("SPARCLI_NO_TTY", "1", 1);
        const int rc = sparcli::edit_file("/tmp/mdtask-nonexistent.md");
        CHECK(rc == -1);
        unsetenv("SPARCLI_NO_TTY");
    }
}
