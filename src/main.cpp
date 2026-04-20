#include "auth/auth_rate_limiter.h"
#include "auth/auth_service.h"
#include "auth/crypto.h"
#include "auth/in_memory_auth_store.h"
#include "auth/postgres_auth_store.h"
#include "auth/session_sweeper.h"
#include "auth/token_cache.h"
#include "chat/chat_service.h"
#include "chat/rate_limiter.h"
#include "config/config.h"
#include "db/connection.h"
#include "event/dispatcher.h"
#include "greeter/greeter.h"
#include "log/logger.h"
#include "net/dispatcher.h"
#include "net/ix_transport.h"
#include "protocol/generated/action_generated.h"
#include "protocol/generated/auth_generated.h"
#include "protocol/generated/chat_generated.h"
#include "protocol/generated/ping_generated.h"
#include "protocol/generated/sim_generated.h"
#include "protocol/generated/world_generated.h"
#include "session/session_manager.h"
#include "sim/action_resolver.h"
#include "sim/sim_dispatcher.h"
#include "sim/sim_loop.h"
#include "world/character_service.h"
#include "world/character_store.h"
#include "world/locations_loader.h"
#include "world/world.h"

#include <atomic>
#include <condition_variable>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>

namespace {

std::atomic<bool> g_shutdown_requested{false};
std::condition_variable g_shutdown_cv;
std::mutex g_shutdown_mu;

void handle_signal(int) {
    g_shutdown_requested.store(true);
    g_shutdown_cv.notify_all();
}

} // namespace

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

    // Initialize libsodium before any crypto code runs — hash_password /
    // verify_password / generate_session_token all assume it has been called.
    try {
        game::auth::crypto::init();
    } catch (const std::exception& e) {
        LOG_ERR("libsodium init failed: {}", e.what());
        return 1;
    }

    auto greeting = game::greeter::get_greeting(cfg.greeting_message);
    LOG_INF("Greeting resolved: {}", greeting);
    std::cout << greeting << std::endl;

    // ── Build the auth + session + net stack ──────────────────────────────
    // Open the DB connection up front only when the Postgres backend is
    // selected. The `memory` backend is retained for local iteration without
    // Postgres (tests, quick bring-up).
    std::unique_ptr<game::db::Connection> db_conn;
    std::unique_ptr<game::auth::IAuthStore> backend_store;  // InMem or Postgres
    game::auth::PostgresAuthStore* pg_store_raw = nullptr;  // for sweeper

    if (cfg.auth.backend == "memory") {
        LOG_INF("auth backend: memory (in-memory store; no persistence)");
        backend_store = std::make_unique<game::auth::InMemoryAuthStore>();
    } else {
        LOG_INF("auth backend: postgres");
        try {
            db_conn = std::make_unique<game::db::Connection>(cfg.database_url);
            auto probe = db_conn->exec("SELECT 1");
            LOG_INF("DB connected (probe returned {} row(s))", probe.rows());
        } catch (const std::exception& e) {
            LOG_ERR("DB connection failed (auth.backend=postgres): {}", e.what());
            return 1;
        }
        auto pg_store = std::make_unique<game::auth::PostgresAuthStore>(*db_conn);
        pg_store_raw = pg_store.get();
        backend_store = std::move(pg_store);
    }

    // Wrap whichever backend with the token cache — even for `memory`, so
    // the hot-path shape is identical across backends and tests in prod-like
    // conditions.
    game::auth::TokenCache::Config tc_cfg;
    tc_cfg.cache_ttl = std::chrono::seconds(cfg.auth.token_cache.ttl_sec);
    tc_cfg.touch_interval = std::chrono::seconds(cfg.auth.session_touch_interval_sec);
    game::auth::TokenCache store(*backend_store, tc_cfg);

    game::auth::AuthRateLimiter::Config rl_cfg;
    rl_cfg.ip_bucket_cap = cfg.auth.rate_limit.ip_bucket_cap;
    rl_cfg.ip_bucket_refill = std::chrono::seconds(cfg.auth.rate_limit.ip_bucket_refill_sec);
    rl_cfg.per_account_hourly_fails = cfg.auth.rate_limit.per_account_hourly_fails;
    game::auth::AuthRateLimiter rate_limiter(rl_cfg);

    game::session::SessionManager sessions;
    game::net::IxTransport transport;
    transport.set_max_connections(cfg.network.max_connections);

    // ── World: locations + characters (in-RAM, sim-thread-owned) ──────────
    game::world::World world;
    try {
        auto graph = game::world::load_locations(cfg.world.locations_file);
        world.install_locations(std::move(graph));
        LOG_INF("World: loaded {} locations from {} (spawn={})",
                world.locations().size(), cfg.world.locations_file,
                world.spawn_location_id());
    } catch (const std::exception& e) {
        LOG_ERR("Failed to load world locations from {}: {}",
                cfg.world.locations_file, e.what());
        return 1;
    }

    // CharacterStore is only available with the Postgres backend (it shares
    // the libpq connection). With memory backend, character creation isn't
    // supported in step 007 — auth still works, AuthOk.characters is empty.
    std::unique_ptr<game::world::CharacterStore> character_store;
    if (db_conn) {
        character_store = std::make_unique<game::world::CharacterStore>(*db_conn);
        try {
            auto chars = character_store->load_all();
            LOG_INF("World: loaded {} characters from DB", chars.size());
            world.install_characters(std::move(chars));
        } catch (const std::exception& e) {
            LOG_ERR("Failed to load characters from DB: {}", e.what());
            return 1;
        }
    } else {
        LOG_WRN("auth.backend=memory: character store disabled — Create/List/Select not available");
    }

    game::auth::AuthService::Config as_cfg;
    as_cfg.session_ttl = std::chrono::hours(24) * cfg.auth.session_ttl_days;
    game::auth::AuthService auth_service(
        store, sessions, rate_limiter,
        [&transport](game::net::ConnId c, const uint8_t* data, size_t len) {
            transport.send(c, data, len);
        },
        as_cfg, character_store.get());

    // ── Sim loop + event dispatcher ───────────────────────────────────────
    game::event::EventDispatcher event_dispatcher(
        sessions,
        [&transport](game::net::ConnId c, const uint8_t* data, size_t len) {
            transport.send(c, data, len);
        });
    event_dispatcher.set_world(&world);
    game::sim::SimLoop sim_loop(world, event_dispatcher,
                                std::chrono::milliseconds(cfg.sim.tick_ms));

    // ActionResolver runs each tick between command drain and world.advance().
    game::sim::ActionResolver action_resolver(sessions);
    sim_loop.set_action_resolver(&action_resolver);

    // ── Chat service (direct IO-thread path, non-sim) ─────────────────────
    game::chat::RateLimiter::Config chat_rl_cfg;
    chat_rl_cfg.local.cap = cfg.chat.local.bucket_cap;
    chat_rl_cfg.local.refill_per_sec = cfg.chat.local.refill_per_sec;
    chat_rl_cfg.global.cap = cfg.chat.global.bucket_cap;
    chat_rl_cfg.global.refill_per_sec = cfg.chat.global.refill_per_sec;
    game::chat::RateLimiter chat_rate_limiter(chat_rl_cfg);

    game::chat::ChatService::Config chat_svc_cfg;
    chat_svc_cfg.max_text_bytes = cfg.chat.max_text_bytes;
    game::chat::ChatService chat_service(
        sessions, chat_rate_limiter, event_dispatcher, sim_loop,
        [&transport](game::net::ConnId c, const uint8_t* data, size_t len) {
            transport.send(c, data, len);
        },
        chat_svc_cfg);

    // ── Sim packet dispatcher (IO thread → command queue) ─────────────────
    game::sim::SimDispatcher sim_packet_dispatcher(sim_loop);

    // ── Character / action service (gated on the DB-backed character store) ─
    std::unique_ptr<game::world::CharacterService> character_service;
    if (character_store) {
        character_service = std::make_unique<game::world::CharacterService>(
            sessions, *character_store, sim_loop, transport,
            world.spawn_location_id(),
            [&transport](game::net::ConnId c, const uint8_t* data, size_t len) {
                transport.send(c, data, len);
            });
    }

    game::net::Dispatcher dispatcher;
    dispatcher.register_handler<::auth::Register>(
        "auth.Register",
        [&](game::net::ConnId c, const ::auth::Register& p) { auth_service.handle_register(c, p); });
    dispatcher.register_handler<::auth::Login>(
        "auth.Login",
        [&](game::net::ConnId c, const ::auth::Login& p) { auth_service.handle_login(c, p); });
    dispatcher.register_handler<::auth::Resume>(
        "auth.Resume",
        [&](game::net::ConnId c, const ::auth::Resume& p) { auth_service.handle_resume(c, p); });
    dispatcher.register_handler<::chat::ChatSay>(
        "chat.ChatSay",
        [&](game::net::ConnId c, const ::chat::ChatSay& p) { chat_service.handle_chat_say(c, p); });
    dispatcher.register_handler<::sim::ClientPing>(
        "sim.ClientPing",
        [&](game::net::ConnId c, const ::sim::ClientPing& p) {
            sim_packet_dispatcher.handle_client_ping(c, p);
        });
    if (character_service) {
        auto* svc = character_service.get();
        dispatcher.register_handler<::world::CreateCharacter>(
            "world.CreateCharacter",
            [svc](game::net::ConnId c, const ::world::CreateCharacter& p) {
                svc->handle_create_character(c, p);
            });
        dispatcher.register_handler<::world::ListCharacters>(
            "world.ListCharacters",
            [svc](game::net::ConnId c, const ::world::ListCharacters& p) {
                svc->handle_list_characters(c, p);
            });
        dispatcher.register_handler<::world::SelectCharacter>(
            "world.SelectCharacter",
            [svc](game::net::ConnId c, const ::world::SelectCharacter& p) {
                svc->handle_select_character(c, p);
            });
        dispatcher.register_handler<::world::DeselectCharacter>(
            "world.DeselectCharacter",
            [svc](game::net::ConnId c, const ::world::DeselectCharacter& p) {
                svc->handle_deselect_character(c, p);
            });
        dispatcher.register_handler<::action::StartAction>(
            "action.StartAction",
            [svc](game::net::ConnId c, const ::action::StartAction& p) {
                svc->handle_start_action(c, p);
            });
        dispatcher.register_handler<::action::CancelAction>(
            "action.CancelAction",
            [svc](game::net::ConnId c, const ::action::CancelAction& p) {
                svc->handle_cancel_action(c, p);
            });
    }

    transport.on_connect = [&](game::net::ConnId c, std::string_view remote) {
        sessions.add(c, std::string(remote));
    };
    transport.on_disconnect = [&](game::net::ConnId c) {
        // Push the world-side cleanup before removing the session — the
        // command captures the character_id from the still-present session,
        // and the sim thread will broadcast CharacterLeft / cancel any action.
        if (character_service) character_service->handle_disconnect(c);
        sessions.remove(c);
    };
    transport.on_message = [&](game::net::ConnId c, const uint8_t* data, size_t len) {
        dispatcher.dispatch(c, data, len);
    };

    try {
        transport.start(cfg.network.port);
    } catch (const std::exception& e) {
        LOG_ERR("Failed to start transport: {}", e.what());
        return 1;
    }

    // Start the sim thread once the transport is accepting — ordering: stop
    // transport → stop sim loop → stop sweeper → close DB on shutdown.
    sim_loop.start();

    // ── Session sweeper (expired Postgres rows + cache eviction) ──────────
    std::unique_ptr<game::auth::SessionSweeper> sweeper;
    if (pg_store_raw) {
        sweeper = std::make_unique<game::auth::SessionSweeper>(
            *pg_store_raw, store,
            std::chrono::seconds(cfg.auth.sweeper_interval_sec));
        sweeper->start();
        LOG_INF("Session sweeper started (interval {}s)", cfg.auth.sweeper_interval_sec);
    }

    // ── Handshake timeout sweep ───────────────────────────────────────────
    const std::chrono::seconds handshake_timeout(cfg.auth.handshake_timeout_sec);
    std::atomic<bool> handshake_sweeper_stop{false};
    std::thread handshake_sweeper([&] {
        while (!handshake_sweeper_stop.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (handshake_sweeper_stop.load()) break;
            auto now = std::chrono::steady_clock::now();
            auto stale = sessions.collect_handshake_timeouts(now, handshake_timeout);
            for (auto c : stale) {
                LOG_INF("auth: handshake timeout on conn {}", c);
                transport.disconnect(c);
            }
        }
    });

    // Protocol smoke test: prove flatc ran and the generated code works.
    {
        flatbuffers::FlatBufferBuilder builder;
        auto encoded = ping::CreatePing(builder, /*nonce=*/42u);
        builder.Finish(encoded);
        const auto* decoded = flatbuffers::GetRoot<ping::Ping>(builder.GetBufferPointer());
        LOG_INF("Ping roundtrip OK (nonce={}, size={} bytes)", decoded->nonce(), builder.GetSize());
    }

    // ── Wait for shutdown signal ──────────────────────────────────────────
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
    LOG_INF("Server up. Waiting for signal (SIGINT / SIGTERM).");

    {
        std::unique_lock<std::mutex> lk(g_shutdown_mu);
        g_shutdown_cv.wait(lk, [] { return g_shutdown_requested.load(); });
    }

    LOG_INF("Shutdown signal received");
    // Shutdown ordering: transport first (stop accepting + stop delivering
    // new packets to handlers), then the sim loop (drain + exit), then the
    // session sweeper and the handshake sweeper, then the DB is closed by
    // RAII as db_conn goes out of scope.
    transport.stop();
    sim_loop.stop();
    handshake_sweeper_stop.store(true);
    if (handshake_sweeper.joinable()) handshake_sweeper.join();
    if (sweeper) sweeper->stop();

    LOG_INF("Shutting down");
    return 0;
}
