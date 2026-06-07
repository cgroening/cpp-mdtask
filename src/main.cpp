#include "cli/add_command.hpp"
#include "cli/archive_command.hpp"
#include "cli/command.hpp"
#include "cli/completion_command.hpp"
#include "cli/config_command.hpp"
#include "cli/done_command.hpp"
#include "cli/edit_command.hpp"
#include "cli/error_output.hpp"
#include "cli/list_command.hpp"
#include "config/config.hpp"
#include "config/config_loader.hpp"
#include "service/task_service.hpp"
#include "storage/markdown_task_repository.hpp"
#include "tui/task_finder.hpp"
#include "util/app_info.hpp"

#include <sparcli.hpp>

#include <memory>
#include <utility>
#include <vector>

namespace {

using mdtask::Command;
using mdtask::Config;

/**
 * Owns the fully wired-up object graph of the application.
 *
 * The config, repository and service live behind unique_ptr so they keep a
 * stable address: when the App is moved (e.g. when returned from make_app),
 * only the pointers travel while the pointed-to objects stay put, so every
 * reference into them (service -> repository, commands -> service/config)
 * stays valid.
 */
struct App {
    std::unique_ptr<Config> config;
    std::unique_ptr<mdtask::TaskRepository> task_repository;
    std::unique_ptr<mdtask::TaskService> task_service;
    std::vector<std::unique_ptr<Command>> commands;
};

/* Forward declarations (defined in call order below main()) */
void setup_theme();
App make_app(Config config);
sparcli::Args build_parser(
    const std::vector<std::unique_ptr<Command>>& commands
);
void setup_logging(const Config& config, bool verbose);

}  // namespace


int main(int argc, char** argv) {
    try {
        // 0. Theme: a purple accent across markup, the CLI and every widget.
        //    Set once up front, before anything renders.
        setup_theme();

        // 1. Load the configuration (file is optional; defaults otherwise)
        auto config = mdtask::load_config();
        if(!config) {
            return mdtask::report_error(config.error());
        }

        // 2. Wire the layers together (composition root)
        App app = make_app(std::move(*config));

        // 3. Describe the CLI and parse argv. The parser renders --help,
        //    --version and error messages itself.
        sparcli::Args args = build_parser(app.commands);
        const auto matched = args.parse(argc, argv);
        if(!matched) {
            return args.exit_code();
        }

        // 4. Configure logging with --verbose
        setup_logging(*app.config, args.get_flag("verbose"));

        // 5. Dispatch to the matched command. A bare invocation (no
        //    subcommand) opens the interactive fuzzy agenda - the main screen.
        if(auto* command = matched->userdata<Command>()) {
            return command->run(args);
        }
        mdtask::run_task_finder(*app.task_service, *app.config);
        return 0;
    } catch(const mdtask::StorageError& error) {
        // Error boundary: unrecoverable storage failures end up here
        sparcli::ErrorReport(error.what())
            .hint("Check file permissions and free disk space")
            .print_stderr();
        return 1;
    } catch(const std::exception& error) {
        // Last resort: report any unexpected error instead of crashing
        sparcli::ErrorReport(error.what()).print_stderr();
        return 1;
    }
}


namespace {

/**
 * Sets the process-wide accent to purple and the input theme.
 *
 * Overriding the palette name "accent" recolors markup, the CLI and every
 * widget default that resolves it (including the fuzzy finder's accent and
 * match highlight). The input theme paints the cursor row of the fuzzy/select
 * widgets with a full-width bar in a dark purple. Call once before rendering.
 */
void setup_theme() {
    sparcli::palette::set("accent", sparcli::palette::purple());
    sparcli::set_theme(sparcli::InputTheme{
        .accent = sparcli::palette::purple(),
        .selected_style = {
            .attr = SC_TEXT_ATTR_BOLD,
            .bg   = sparcli::rgb(57, 32, 82),
        },
    });
}

/**
 * Composition root: constructs the concrete types and wires the layers.
 *
 * This is the only place that knows which repository implementation is used
 * and which commands exist. Swapping the storage backend or adding a
 * command is a local change here.
 */
App make_app(Config config) {
    auto owned_config = std::make_unique<Config>(std::move(config));
    auto task_repository = std::make_unique<mdtask::MarkdownTaskRepository>(
        owned_config->tasks_dir, owned_config->archive_dir
    );
    auto task_service = std::make_unique<mdtask::TaskService>(
        *task_repository
    );

    std::vector<std::unique_ptr<Command>> commands;
    commands.push_back(
        std::make_unique<mdtask::AddCommand>(*task_service)
    );
    commands.push_back(
        std::make_unique<mdtask::ListCommand>(*task_service)
    );
    commands.push_back(
        std::make_unique<mdtask::DoneCommand>(*task_service)
    );
    commands.push_back(
        std::make_unique<mdtask::EditCommand>(*task_service, *owned_config)
    );
    commands.push_back(
        std::make_unique<mdtask::ArchiveCommand>(*task_service)
    );
    commands.push_back(
        std::make_unique<mdtask::ConfigCommand>(*owned_config)
    );
    commands.push_back(std::make_unique<mdtask::CompletionCommand>());

    return App{
        .config          = std::move(owned_config),
        .task_repository = std::move(task_repository),
        .task_service    = std::move(task_service),
        .commands        = std::move(commands),
    };
}

/**
 * Builds the declarative argument parser from the registered commands.
 *
 * Each command contributes one subcommand node and configures its own
 * arguments on it; global flags live on the root.
 */
sparcli::Args build_parser(
    const std::vector<std::unique_ptr<Command>>& commands
) {
    sparcli::Args args({
        .prog    = mdtask::APP_NAME,
        .version = mdtask::APP_VERSION,
        .about   = mdtask::APP_ABOUT,
    });

    auto root = args.root();
    root.flag("verbose", 'v', "Enable debug logging on stderr");

    for(const auto& command : commands) {
        auto node = root.subcommand(command->name(), command->summary());
        command->configure(node);
        // Store a back-pointer so the matched node maps straight to the code
        // that runs it - no separate dispatch table, no name compare.
        node.userdata(command.get());
    }
    return args;
}

/**
 * Configures the process-wide logger.
 *
 * The stderr sink uses the configured level (or DEBUG with --verbose); the
 * plain-text file sink in the XDG state directory always records DEBUG so
 * problems can be diagnosed after the fact.
 */
void setup_logging(const Config& config, bool verbose) {
    sparcli::logging::set_level(verbose ? SC_LOG_DEBUG : config.log_level);

    if(!config.log_file.empty()) {
        sparcli::logging::add_file(config.log_file.string(), SC_LOG_DEBUG);
    }

    sparcli::logging::debug("logging initialized");
}

}  // namespace
