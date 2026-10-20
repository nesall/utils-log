# logger.hpp

Header-only, thread-safe, cross-platform logging utility (C++17).

Two independent outputs:

* `output.log`: general-purpose logging with levels
* `diagnostics.log`: crash-point detection based on function/block scopes

## Usage

```cpp
#include "logger.hpp"

void foo() {
  LOG_START;                          // scope logged to diagnostics.log
  LOG_MSG << "Doing stuff" << 123 << true;
  if (g()) {
    LOG_START1("if block");           // named scope
  }
  LOG_HERE("Halfway");                // checkpoint inside the current scope
}

int main() {
  SET_LOG_OUTPUT_FILE_PATH("app.log");
  SET_LOG_MIN_LEVEL(utils_log::Level::debug);

  foo();
  LOG_MSG_WARN << "Low disk space:" << 42 << "MB";
  LOG_MSG_ERROR << "Failed to open" << "config.json";
  LOG_MSGNF << "This only logs to console";

  utils_log::Log::terminate();        // optional: close the file explicitly
}
```

Arguments are space-separated by default. Use `LOG_NOSPACE` / `LOG_SPACE` to toggle:

```cpp
LOG_MSG << "a" << LOG_NOSPACE << "b" << "c" << LOG_SPACE << "d";   // a bc d
```

## Levels

| Macro             | Level   | Console stream |
|-------------------|---------|----------------|
| `LOG_MSG_DEBUG`   | debug   | stdout         |
| `LOG_MSG_INFO`    | info    | stdout         |
| `LOG_MSG_WARN`    | warn    | stderr         |
| `LOG_MSG_ERROR`   | error   | stderr         |
| `LOG_MSG`         | info (alias of `LOG_MSG_INFO`) | stdout |
| `LOG_MSGNF`       | info, **no file** (console only) | stdout |

* Default minimum level is `info`. Change at runtime with `SET_LOG_MIN_LEVEL(level)`.
* Messages below the minimum level are skipped **without evaluating their operands**, so debug logging in hot paths is cheap.
* The `LOG_MSG_*` names are chosen to avoid collisions with `<syslog.h>` (`LOG_INFO`, ...) and `windows.h` (`ERROR`).
* Under Qt (`QT_CORE_LIB`), console output goes through `qDebug()` / `qWarning()`.
* On Windows, console output is also sent to `OutputDebugStringA`.

## Configuration

| Macro                                  | Effect                                                   |
|----------------------------------------|----------------------------------------------------------|
| `SET_LOG_OUTPUT_FILE_PATH(path)`       | Output log file (default `output.log`). Safe at runtime. |
| `SET_LOG_DIAGNOSTICS_FILE_PATH(path)`  | Diagnostics file (default `diagnostics.log`).            |
| `SET_LOG_TO_FILE(bool)`                | Default file logging for new `Log` instances.            |
| `SET_LOG_TO_CONSOLE(bool)`             | Default console logging for new `Log` instances.         |
| `SET_LOG_MIN_LEVEL(level)`             | Minimum level to emit.                                   |

Set paths before the first log call when possible. Changing them later closes the current file and reopens at the new path on the next write.

## Output format

`output.log`:

```
[2026-10-20 14:03:07.512] [WARN ] tid=1234567890 "Low disk space: 42 MB"
```

`tid` is a stable hash of the thread ID, consistent within a process.

`diagnostics.log`:

```
[2026-10-20 14:03:07] foo:start... C:\src\main.cpp |1
[2026-10-20 14:03:07] foo:end! C:\src\main.cpp |0
```

The number after `|` is the count of currently open scopes.

## Rotation

* `output.log` rotates at runtime when it exceeds 5 MB. The current file becomes `output.log.old` (one backup, overwritten on each rotation).
* `diagnostics.log` rotates at startup only, when it exceeds 2 MB.
* Rotation failures (e.g. file locked by another instance) are ignored and never throw.

## Crash detection

Every scope start/end is flushed to `diagnostics.log` immediately. On the next startup, if the last line shows open scopes (`|N` with N > 0), the process is assumed to have crashed and `## CRASH POINT ##` is written. The lines above it show the scopes that were active.

Notes:

* Detection happens before rotation, so a crash is not lost to a rotated file.
* The depth counter is process-wide, not per-thread. With several threads, read the log as "scopes open in the process" rather than a per-thread call stack.
* `LOG_START` / `LOG_START1` / `LOG_HERE` flush on every call. They are meant for diagnosing crashes, not for hot paths.

## Notes

* Logging never throws. Failures inside `~Log()` are swallowed.
* Console writes and file writes are serialized by a global mutex.
* Don't log from static destructors of other translation units, since the file stream may already be destroyed.
* Requires C++17 (`<filesystem>`, `inline` variables).