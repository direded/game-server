// Initialize the Quill logging backend once per test process. auth_service
// (and anything else that uses LOG_*) relies on game::log::init having been
// called — otherwise server_logger() returns nullptr and the macros crash.
//
// A plain static initializer is enough here: gtest_main runs tests from
// main(), which starts well after C++ static init.

#include "log/logger.h"

namespace {

struct TestLoggerInit {
    TestLoggerInit() {
        game::log::init("logs/test.log", "error");
    }
};

static TestLoggerInit g_test_logger_init;

} // namespace
