#pragma once

// Test entry points. Each runs one group of CHECKs (see check.hpp) and is
// invoked by the runner in test_main.cpp.
void run_agenda_tests();
void run_subtasks_tests();
void run_task_presentation_tests();
void run_finder_actions_tests();
void run_service_tests();
void run_markdown_document_tests();
void run_markdown_task_repository_tests();
void run_config_loader_tests();
