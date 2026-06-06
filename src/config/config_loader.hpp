#pragma once

#include "config/config.hpp"
#include "domain/errors.hpp"

namespace mdtask {

/**
 * Resolves the application configuration into a typed Config.
 *
 * Layers, lowest to highest precedence:
 *
 *   1. built-in defaults (data/log paths from sparcli's XDG helpers),
 *   2. the optional TOML file `$XDG_CONFIG_HOME/mdtask/config.toml`,
 *   3. environment overrides under the `MDTASK_` prefix (e.g.
 *      `MDTASK_TASKS_DIR`, `MDTASK_LOG_LEVEL`).
 *
 * sparcli's layered config (sparcli::Config) does the parsing and merging;
 * this function maps the merged values into the typed Config the rest of the
 * application uses. Recognized keys:
 *
 *   - `log_level`   - one of: debug, info, warn, error, off
 *   - `tasks_dir`   - directory holding the active task files
 *   - `archive_dir` - root for archived files (default: `<tasks_dir>/archive`)
 *   - `log_file`    - path of the debug log (empty disables it)
 *   - `date_format` - one of: dmy, iso
 *   - `editor`      - editor for the description (empty: $EDITOR, then nvim)
 *
 * A missing file is not an error: the defaults are used. A malformed file or
 * an invalid `log_level`/`date_format` is a CONFIG_INVALID error (fail fast)
 * so typos in the config never go unnoticed.
 *
 * @return The resolved configuration, or a CONFIG_INVALID error.
 */
[[nodiscard]] Result<Config> load_config();

}  // namespace mdtask
