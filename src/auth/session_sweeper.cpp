#include "auth/session_sweeper.h"

#include "auth/postgres_auth_store.h"
#include "auth/token_cache.h"
#include "log/logger.h"

#include <chrono>
#include <exception>

namespace game::auth {

SessionSweeper::SessionSweeper(PostgresAuthStore& store,
                               TokenCache& cache,
                               std::chrono::seconds interval)
    : store_(store), cache_(cache), interval_(interval) {}

SessionSweeper::~SessionSweeper() { stop(); }

void SessionSweeper::start() {
    stop_.store(false);
    thread_ = std::thread([this] { run(); });
}

void SessionSweeper::stop() {
    stop_.store(true);
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

size_t SessionSweeper::sweep_once() {
    std::vector<std::string> deleted;
    try {
        deleted = store_.delete_expired_sessions(Clock::now());
    } catch (const std::exception& e) {
        LOG_WRN("session sweeper: delete_expired_sessions failed: {}", e.what());
        return 0;
    }
    for (const auto& tok : deleted) {
        cache_.invalidate(tok);
    }
    if (!deleted.empty()) {
        LOG_INF("session sweeper: pruned {} expired session(s)", deleted.size());
    }
    return deleted.size();
}

void SessionSweeper::run() {
    while (!stop_.load()) {
        sweep_once();
        std::unique_lock<std::mutex> lk(mu_);
        cv_.wait_for(lk, interval_, [this] { return stop_.load(); });
    }
}

}  // namespace game::auth
