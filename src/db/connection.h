#pragma once

#include <libpq-fe.h>

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace game::db {

struct QueryError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// RAII wrapper around PGresult.
class Result {
public:
    explicit Result(PGresult* res) : res_(res) {}
    ~Result() { if (res_) PQclear(res_); }
    Result(Result&& o) noexcept : res_(std::exchange(o.res_, nullptr)) {}
    Result& operator=(Result&& o) noexcept {
        if (this != &o) { if (res_) PQclear(res_); res_ = std::exchange(o.res_, nullptr); }
        return *this;
    }
    Result(const Result&) = delete;
    Result& operator=(const Result&) = delete;

    PGresult* get() const { return res_; }
    int rows() const { return PQntuples(res_); }
    int cols() const { return PQnfields(res_); }
    std::string_view at(int r, int c) const { return PQgetvalue(res_, r, c); }

private:
    PGresult* res_;
};

// RAII wrapper around PGconn. Thread-unsafe — one per thread/coroutine.
class Connection {
public:
    // conninfo is a libpq connection string or URI, e.g.
    //   "postgresql://user:pw@host:5432/dbname?sslmode=disable"
    explicit Connection(const std::string& conninfo);
    ~Connection();
    Connection(Connection&&) noexcept;
    Connection& operator=(Connection&&) noexcept;
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    // True if the underlying socket is still usable.
    bool ok() const;

    // Runs a parameterless statement. Throws QueryError on failure.
    Result exec(const std::string& sql);

    // Runs a parameterised statement with text-format parameters.
    Result exec_params(const std::string& sql, const std::vector<std::string>& params);

private:
    PGconn* conn_ = nullptr;
};

} // namespace game::db
