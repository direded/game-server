#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace game::auth {

class PostgresAuthStore;
class TokenCache;

// Background thread that periodically deletes expired session rows from the
// Postgres store and evicts matching cache entries. The sweep interval is
// coarse (default 10 min) because expiry is also enforced on the hot path:
// auth_service rejects resume attempts whose expires_at has passed even if
// the row is still present.
class SessionSweeper {
public:
    // Neither pointer is owning; both must outlive the sweeper.
    SessionSweeper(PostgresAuthStore& store,
                   TokenCache& cache,
                   std::chrono::seconds interval);
    ~SessionSweeper();
    SessionSweeper(const SessionSweeper&) = delete;
    SessionSweeper& operator=(const SessionSweeper&) = delete;

    void start();
    void stop();  // idempotent; safe from any thread

    // Synchronously sweep once. Exposed for tests — production uses the
    // loop in start(). Returns the number of sessions deleted.
    size_t sweep_once();

private:
    void run();

    PostgresAuthStore& store_;
    TokenCache& cache_;
    std::chrono::seconds interval_;

    std::atomic<bool> stop_{false};
    std::thread thread_;
    std::mutex mu_;
    std::condition_variable cv_;
};

}  // namespace game::auth
