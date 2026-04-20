#pragma once

#include <yaml-cpp/yaml.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <stdexcept>

namespace game::config {

struct NetworkConfig {
    uint16_t port = 7777;
    size_t max_connections = 10000;
};

struct AuthRateLimitConfig {
    size_t ip_bucket_cap = 10;
    uint32_t ip_bucket_refill_sec = 6;
    size_t per_account_hourly_fails = 20;
};

struct AuthConfig {
    uint32_t session_ttl_days = 30;
    uint32_t handshake_timeout_sec = 10;
    AuthRateLimitConfig rate_limit;
};

struct Config {
    // greeting
    std::string greeting_message = "Hello, world!";

    // logging
    std::string log_level = "info";
    std::string log_file = "logs/game-server.log";

    // database (libpq conninfo string / URI)
    std::string database_url = "postgresql://postgres:postgres@localhost:5432/game?sslmode=disable";

    // network / auth (step 004)
    NetworkConfig network;
    AuthConfig auth;

    static Config load(const std::string& path) {
        Config cfg;
        YAML::Node root = YAML::LoadFile(path);

        if (auto greeting = root["greeting"]; greeting && greeting.IsMap()) {
            if (greeting["message"]) cfg.greeting_message = greeting["message"].as<std::string>(cfg.greeting_message);
        }

        if (auto logging = root["logging"]; logging && logging.IsMap()) {
            if (logging["level"]) cfg.log_level = logging["level"].as<std::string>(cfg.log_level);
            if (logging["file"])  cfg.log_file  = logging["file"].as<std::string>(cfg.log_file);
        }

        if (auto database = root["database"]; database && database.IsMap()) {
            if (database["url"]) cfg.database_url = database["url"].as<std::string>(cfg.database_url);
        }

        if (auto network = root["network"]; network && network.IsMap()) {
            if (network["port"]) cfg.network.port = network["port"].as<uint16_t>(cfg.network.port);
            if (network["max_connections"]) cfg.network.max_connections = network["max_connections"].as<size_t>(cfg.network.max_connections);
        }

        if (auto auth = root["auth"]; auth && auth.IsMap()) {
            if (auth["session_ttl_days"]) cfg.auth.session_ttl_days = auth["session_ttl_days"].as<uint32_t>(cfg.auth.session_ttl_days);
            if (auth["handshake_timeout_sec"]) cfg.auth.handshake_timeout_sec = auth["handshake_timeout_sec"].as<uint32_t>(cfg.auth.handshake_timeout_sec);

            if (auto rl = auth["rate_limit"]; rl && rl.IsMap()) {
                if (rl["ip_bucket_cap"]) cfg.auth.rate_limit.ip_bucket_cap = rl["ip_bucket_cap"].as<size_t>(cfg.auth.rate_limit.ip_bucket_cap);
                if (rl["ip_bucket_refill_sec"]) cfg.auth.rate_limit.ip_bucket_refill_sec = rl["ip_bucket_refill_sec"].as<uint32_t>(cfg.auth.rate_limit.ip_bucket_refill_sec);
                if (rl["per_account_hourly_fails"]) cfg.auth.rate_limit.per_account_hourly_fails = rl["per_account_hourly_fails"].as<size_t>(cfg.auth.rate_limit.per_account_hourly_fails);
            }
        }

        return cfg;
    }
};

inline Config& get() {
    static Config instance;
    return instance;
}

inline void load(const std::string& path) {
    get() = Config::load(path);
}

} // namespace game::config
