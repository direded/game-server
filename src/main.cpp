#include "config/config.h"
#include "db/connection.h"
#include "greeter/greeter.h"
#include "log/logger.h"
#include "protocol/generated/ping_generated.h"

#include <filesystem>
#include <iostream>

int main(int argc, char* argv[]) {
    std::string config_path = "config.yaml";
    if (argc > 1) {
        config_path = argv[1];
    }

    // Fallback: if config.yaml isn't at CWD, look next to the executable (VS debugger case)
    if (!std::filesystem::exists(config_path)) {
        auto exe_dir = std::filesystem::path(argv[0]).parent_path();
        auto alt_path = exe_dir / ".." / ".." / "config.yaml";
        if (std::filesystem::exists(alt_path)) {
            config_path = alt_path.string();
        }
    }

    try {
        game::config::load(config_path);
    } catch (const std::exception& e) {
        std::cerr << "Failed to load config (" << config_path << "): " << e.what() << std::endl;
        std::cerr << "Usage: game-server [config.yaml path]" << std::endl;
        return 1;
    }

    auto& cfg = game::config::get();

    auto log_dir = std::filesystem::path(cfg.log_file).parent_path();
    if (!log_dir.empty()) {
        std::filesystem::create_directories(log_dir);
    }

    game::log::init(cfg.log_file, cfg.log_level);
    LOG_INF("Starting game server");
    LOG_INF("Config loaded from: {}", config_path);

    auto greeting = game::greeter::get_greeting(cfg.greeting_message);
    LOG_INF("Greeting resolved: {}", greeting);

    std::cout << greeting << std::endl;
    LOG_INF("Printed greeting to stdout");

    // Ping the database. Warn-only — server still starts if DB is unreachable,
    // so dev can work on non-DB features without Postgres running.
    try {
        game::db::Connection conn(cfg.database_url);
        auto res = conn.exec("SELECT COUNT(*) FROM dummy");
        LOG_INF("DB connected. dummy rows = {}", res.at(0, 0));
    } catch (const std::exception& e) {
        LOG_WRN("DB unavailable: {}", e.what());
    }

    // Protocol smoke test: prove flatc ran and the generated code works
    // end-to-end by building a Ping, serializing it, and reading it back.
    {
        flatbuffers::FlatBufferBuilder builder;
        auto encoded = ping::CreatePing(builder, /*nonce=*/42u);
        builder.Finish(encoded);

        const uint8_t* buf = builder.GetBufferPointer();
        const auto size    = builder.GetSize();
        const auto* decoded = ping::GetPing(buf);

        if (decoded->nonce() == 42u) {
            LOG_INF("Ping flatbuffers roundtrip OK (nonce={}, size={} bytes)",
                    decoded->nonce(), size);
        } else {
            LOG_ERR("Ping flatbuffers roundtrip FAILED: expected nonce=42, got {}",
                    decoded->nonce());
        }
    }

    LOG_INF("Shutting down");
    return 0;
}
