#include "auth/postgres_auth_store.h"

#include "log/logger.h"

#include <libpq-fe.h>

#include <charconv>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <stdexcept>

namespace game::auth {

namespace {

// ISO-8601 UTC with microsecond precision. Postgres parses this cleanly as
// timestamptz.
std::string to_iso8601_utc(Clock::time_point tp) {
    const auto tt = Clock::to_time_t(tp);
    const auto total_us = std::chrono::duration_cast<std::chrono::microseconds>(
                              tp.time_since_epoch()).count();
    const long long frac_us = total_us % 1'000'000;

    std::tm tm_utc{};
#ifdef _WIN32
    gmtime_s(&tm_utc, &tt);
#else
    gmtime_r(&tt, &tm_utc);
#endif
    char buf[40];
    std::snprintf(buf, sizeof(buf),
                  "%04d-%02d-%02dT%02d:%02d:%02d.%06lldZ",
                  tm_utc.tm_year + 1900, tm_utc.tm_mon + 1, tm_utc.tm_mday,
                  tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec,
                  frac_us);
    return buf;
}

// SELECT queries project timestamps as microseconds-since-epoch (bigint),
// so parsing is a single strtoll. Avoids Postgres's configurable text
// formatting.
Clock::time_point from_epoch_us_text(std::string_view s) {
    long long us = 0;
    std::from_chars(s.data(), s.data() + s.size(), us);
    return Clock::time_point(std::chrono::microseconds(us));
}

AccountId to_account_id(std::string_view s) {
    long long v = 0;
    std::from_chars(s.data(), s.data() + s.size(), v);
    return static_cast<AccountId>(v);
}

// Postgres SQLSTATE for unique_violation — only one we care about here.
// See https://www.postgresql.org/docs/current/errcodes-appendix.html.
constexpr const char* kSqlStateUniqueViolation = "23505";

// Check whether the thrown QueryError came from a unique-violation.
// QueryError.what() includes PQresultErrorMessage — libpq doesn't expose
// SQLSTATE there directly. Fallback: substring-match the message. This is
// fragile but acceptable as a single narrow signal, and guarded by the
// CREATE TABLE ... UNIQUE constraint on `username`.
bool is_unique_violation(const std::exception& e) {
    std::string_view msg = e.what();
    // libpq error messages include "duplicate key value violates unique
    // constraint" in English; the SQLSTATE 23505 hex is not in the message.
    // Guard both substrings so future locale changes fall through gracefully.
    return msg.find("23505") != std::string_view::npos
        || msg.find("duplicate key") != std::string_view::npos
        || msg.find("unique constraint") != std::string_view::npos;
}

}  // namespace

PostgresAuthStore::PostgresAuthStore(db::Connection& conn) : conn_(conn) {}

std::optional<AccountRecord> PostgresAuthStore::find_account_by_username(
    std::string_view username) {
    std::lock_guard<std::mutex> lg(mu_);

    const std::string sql =
        "SELECT id, username, password_hash, COALESCE(email, '') "
        "FROM accounts WHERE username = $1::citext";
    auto res = conn_.exec_params(sql, {std::string(username)});
    if (res.rows() == 0) return std::nullopt;

    AccountRecord rec;
    rec.id = to_account_id(res.at(0, 0));
    rec.username = std::string(res.at(0, 1));
    rec.password_hash = std::string(res.at(0, 2));
    rec.email = std::string(res.at(0, 3));
    return rec;
}

std::optional<AccountRecord> PostgresAuthStore::find_account_by_id(AccountId id) {
    std::lock_guard<std::mutex> lg(mu_);

    const std::string sql =
        "SELECT id, username, password_hash, COALESCE(email, '') "
        "FROM accounts WHERE id = $1::bigint";
    auto res = conn_.exec_params(sql, {std::to_string(id)});
    if (res.rows() == 0) return std::nullopt;

    AccountRecord rec;
    rec.id = to_account_id(res.at(0, 0));
    rec.username = std::string(res.at(0, 1));
    rec.password_hash = std::string(res.at(0, 2));
    rec.email = std::string(res.at(0, 3));
    return rec;
}

AccountRecord PostgresAuthStore::create_account(std::string_view username,
                                                std::string_view password_hash,
                                                std::string_view email) {
    std::lock_guard<std::mutex> lg(mu_);

    // Email column is NULLable; treat empty-string as NULL to keep the
    // citext UNIQUE semantics consistent on future email lookups.
    const std::string sql =
        "INSERT INTO accounts (username, password_hash, email) "
        "VALUES ($1::citext, $2, NULLIF($3, '')::citext) "
        "RETURNING id";

    auto res = conn_.exec_params(sql, {
        std::string(username),
        std::string(password_hash),
        std::string(email),
    });
    AccountRecord rec;
    rec.id = to_account_id(res.at(0, 0));
    rec.username = std::string(username);
    rec.password_hash = std::string(password_hash);
    rec.email = std::string(email);
    return rec;
}

SessionRecord PostgresAuthStore::create_session(AccountId account_id,
                                                std::string token,
                                                Clock::time_point created_at,
                                                Clock::time_point expires_at) {
    std::lock_guard<std::mutex> lg(mu_);

    const std::string sql =
        "INSERT INTO sessions (token, account_id, created_at, expires_at, last_seen_at) "
        "VALUES ($1, $2::bigint, $3::timestamptz, $4::timestamptz, $3::timestamptz)";
    conn_.exec_params(sql, {
        token,
        std::to_string(account_id),
        to_iso8601_utc(created_at),
        to_iso8601_utc(expires_at),
    });

    SessionRecord rec;
    rec.token = std::move(token);
    rec.account_id = account_id;
    rec.created_at = created_at;
    rec.expires_at = expires_at;
    rec.last_seen_at = created_at;
    return rec;
}

std::optional<SessionRecord> PostgresAuthStore::find_session(std::string_view token) {
    std::lock_guard<std::mutex> lg(mu_);

    const std::string sql =
        "SELECT token, account_id, "
        "       (EXTRACT(EPOCH FROM created_at)   * 1000000)::bigint, "
        "       (EXTRACT(EPOCH FROM expires_at)   * 1000000)::bigint, "
        "       (EXTRACT(EPOCH FROM last_seen_at) * 1000000)::bigint "
        "FROM sessions WHERE token = $1";
    auto res = conn_.exec_params(sql, {std::string(token)});
    if (res.rows() == 0) return std::nullopt;

    SessionRecord rec;
    rec.token        = std::string(res.at(0, 0));
    rec.account_id   = to_account_id(res.at(0, 1));
    rec.created_at   = from_epoch_us_text(res.at(0, 2));
    rec.expires_at   = from_epoch_us_text(res.at(0, 3));
    rec.last_seen_at = from_epoch_us_text(res.at(0, 4));
    return rec;
}

void PostgresAuthStore::touch_session(std::string_view token,
                                      Clock::time_point last_seen_at,
                                      Clock::time_point expires_at) {
    std::lock_guard<std::mutex> lg(mu_);

    const std::string sql =
        "UPDATE sessions SET last_seen_at = $2::timestamptz, "
        "                    expires_at   = $3::timestamptz "
        "WHERE token = $1";
    conn_.exec_params(sql, {
        std::string(token),
        to_iso8601_utc(last_seen_at),
        to_iso8601_utc(expires_at),
    });
}

void PostgresAuthStore::delete_session(std::string_view token) {
    std::lock_guard<std::mutex> lg(mu_);
    conn_.exec_params("DELETE FROM sessions WHERE token = $1",
                      {std::string(token)});
}

void PostgresAuthStore::delete_sessions_for_account(AccountId account_id) {
    std::lock_guard<std::mutex> lg(mu_);
    conn_.exec_params("DELETE FROM sessions WHERE account_id = $1::bigint",
                      {std::to_string(account_id)});
}

std::vector<std::string> PostgresAuthStore::delete_expired_sessions(
    Clock::time_point now) {
    std::lock_guard<std::mutex> lg(mu_);

    const std::string sql =
        "DELETE FROM sessions WHERE expires_at < $1::timestamptz RETURNING token";
    auto res = conn_.exec_params(sql, {to_iso8601_utc(now)});

    std::vector<std::string> out;
    out.reserve(static_cast<size_t>(res.rows()));
    for (int i = 0; i < res.rows(); ++i) {
        out.emplace_back(res.at(i, 0));
    }
    return out;
}

}  // namespace game::auth
