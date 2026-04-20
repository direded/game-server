// Initialize the Quill logging backend once per test process. auth_service
// (and anything else that uses LOG_*) relies on game::log::init having been
// called — otherwise server_logger() returns nullptr and the macros crash.
//
// A plain static initializer is enough here: gtest_main runs tests from
// main(), which starts well after C++ static init.

#include "log/logger.h"

#include <filesystem>

namespace {

// Resolve `<server>/logs/test.log` from this source file's path so the log
// always lands under server/, regardless of the cwd the test binary was
// launched in (running the wrapper from the umbrella cwd would otherwise
// scatter a stray logs/ folder into the parent repo).
std::string resolve_log_path() {
    namespace fs = std::filesystem;
    // __FILE__ → server/tests/test_logger_init.cpp; up two levels → server/.
    fs::path here(__FILE__);
    fs::path server_root = here.parent_path().parent_path();
    return (server_root / "logs" / "test.log").string();
}

struct TestLoggerInit {
    TestLoggerInit() {
        game::log::init(resolve_log_path(), "error");
    }
};

static TestLoggerInit g_test_logger_init;

} // namespace
