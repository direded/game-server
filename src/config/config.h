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

struct TokenCacheConfig {
    uint32_t ttl_sec = 600;  // how long a cache entry stays fresh
};

struct SimConfig {
    // Tick period in milliseconds. 250 ms = 4 TPS — the step-006 default.
    // Do not crank this lower without also tuning the event/command queues;
    // short ticks amplify any per-tick fixed cost.
    uint32_t tick_ms = 250;
};

struct ChatBucketConfig {
    double bucket_cap = 5.0;
    double refill_per_sec = 1.0;
};

struct WorldConfig {
    // Path (relative to the server binary) to the locations YAML loaded by
    // locations_loader at startup. The graph is immutable for the server's
    // lifetime — see step-007 scope notes.
    std::string locations_file = "config/locations.yaml";
};

struct ChatConfig {
    // Maximum UTF-8 byte length of a ChatSay.text before it is silently dropped.
    size_t max_text_bytes = 500;
    // Per-(account, channel) token buckets. Local defaults are generous enough
    // for normal room chatter; Global is deliberately tight — a stray macro
    // can't spam the whole server.
    ChatBucketConfig local{5.0, 1.0};
    ChatBucketConfig global{3.0, 0.2};
};

struct AuthConfig {
    // "postgres" (default) or "memory". memory retained for local dev /
    // CI without Postgres — step-004 in-memory store behind the same
    // IAuthStore interface.
    std::string backend = "postgres";
    uint32_t session_ttl_days = 30;
    uint32_t handshake_timeout_sec = 10;
    // How often (seconds) the background thread scans `sessions` for rows
    // whose expires_at has passed and deletes them.
    uint32_t sweeper_interval_sec = 600;
    // Minimum gap between store writes for the same token's touch_session.
    uint32_t session_touch_interval_sec = 300;
    TokenCacheConfig token_cache;
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

    // network / auth (step 004) / sim + chat (step 006) / world (step 007)
    NetworkConfig network;
    AuthConfig auth;
    SimConfig sim;
    ChatConfig chat;
    WorldConfig world;

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
            if (auth["backend"]) cfg.auth.backend = auth["backend"].as<std::string>(cfg.auth.backend);
            if (auth["session_ttl_days"]) cfg.auth.session_ttl_days = auth["session_ttl_days"].as<uint32_t>(cfg.auth.session_ttl_days);
            if (auth["handshake_timeout_sec"]) cfg.auth.handshake_timeout_sec = auth["handshake_timeout_sec"].as<uint32_t>(cfg.auth.handshake_timeout_sec);
            if (auth["sweeper_interval_sec"]) cfg.auth.sweeper_interval_sec = auth["sweeper_interval_sec"].as<uint32_t>(cfg.auth.sweeper_interval_sec);
            if (auth["session_touch_interval_sec"]) cfg.auth.session_touch_interval_sec = auth["session_touch_interval_sec"].as<uint32_t>(cfg.auth.session_touch_interval_sec);

            if (auto tc = auth["token_cache"]; tc && tc.IsMap()) {
                if (tc["ttl_sec"]) cfg.auth.token_cache.ttl_sec = tc["ttl_sec"].as<uint32_t>(cfg.auth.token_cache.ttl_sec);
            }

            if (auto rl = auth["rate_limit"]; rl && rl.IsMap()) {
                if (rl["ip_bucket_cap"]) cfg.auth.rate_limit.ip_bucket_cap = rl["ip_bucket_cap"].as<size_t>(cfg.auth.rate_limit.ip_bucket_cap);
                if (rl["ip_bucket_refill_sec"]) cfg.auth.rate_limit.ip_bucket_refill_sec = rl["ip_bucket_refill_sec"].as<uint32_t>(cfg.auth.rate_limit.ip_bucket_refill_sec);
                if (rl["per_account_hourly_fails"]) cfg.auth.rate_limit.per_account_hourly_fails = rl["per_account_hourly_fails"].as<size_t>(cfg.auth.rate_limit.per_account_hourly_fails);
            }
        }

        if (auto sim = root["sim"]; sim && sim.IsMap()) {
            if (sim["tick_ms"]) cfg.sim.tick_ms = sim["tick_ms"].as<uint32_t>(cfg.sim.tick_ms);
        }

        if (auto world = root["world"]; world && world.IsMap()) {
            if (world["locations_file"]) cfg.world.locations_file = world["locations_file"].as<std::string>(cfg.world.locations_file);
        }

        if (auto chat = root["chat"]; chat && chat.IsMap()) {
            if (chat["max_text_bytes"]) cfg.chat.max_text_bytes = chat["max_text_bytes"].as<size_t>(cfg.chat.max_text_bytes);
            if (auto local = chat["local"]; local && local.IsMap()) {
                if (local["bucket_cap"]) cfg.chat.local.bucket_cap = local["bucket_cap"].as<double>(cfg.chat.local.bucket_cap);
                if (local["refill_per_sec"]) cfg.chat.local.refill_per_sec = local["refill_per_sec"].as<double>(cfg.chat.local.refill_per_sec);
            }
            if (auto global = chat["global"]; global && global.IsMap()) {
                if (global["bucket_cap"]) cfg.chat.global.bucket_cap = global["bucket_cap"].as<double>(cfg.chat.global.bucket_cap);
                if (global["refill_per_sec"]) cfg.chat.global.refill_per_sec = global["refill_per_sec"].as<double>(cfg.chat.global.refill_per_sec);
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
