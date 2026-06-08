#include "check.hpp"
#include "test_suite.hpp"

#include <sparcli.hpp>

int main() {
    // Keep the test output clean: the service/repository layers log via the
    // global sparcli logger, which defaults to INFO on stderr.
    sparcli::logging::set_level(SC_LOG_OFF);

    run_agenda_tests();
    run_subtasks_tests();
    run_suggestion_tests();
    run_duplicate_tests();
    run_task_presentation_tests();
    run_finder_actions_tests();
    run_service_tests();
    run_markdown_document_tests();
    run_markdown_task_repository_tests();
    run_config_loader_tests();
    run_editor_tests();
    return check::summary("mdtask");
}
