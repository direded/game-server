#include "db/connection.h"

#include <vector>

namespace game::db {

Connection::Connection(const std::string& conninfo) {
    conn_ = PQconnectdb(conninfo.c_str());
    if (PQstatus(conn_) != CONNECTION_OK) {
        std::string msg = PQerrorMessage(conn_);
        PQfinish(conn_);
        conn_ = nullptr;
        throw QueryError("PQconnectdb failed: " + msg);
    }
}

Connection::~Connection() {
    if (conn_) PQfinish(conn_);
}

Connection::Connection(Connection&& o) noexcept : conn_(std::exchange(o.conn_, nullptr)) {}

Connection& Connection::operator=(Connection&& o) noexcept {
    if (this != &o) {
        if (conn_) PQfinish(conn_);
        conn_ = std::exchange(o.conn_, nullptr);
    }
    return *this;
}

bool Connection::ok() const {
    return conn_ && PQstatus(conn_) == CONNECTION_OK;
}

Result Connection::exec(const std::string& sql) {
    PGresult* res = PQexec(conn_, sql.c_str());
    auto status = PQresultStatus(res);
    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
        std::string msg = PQresultErrorMessage(res);
        PQclear(res);
        throw QueryError("query failed: " + msg);
    }
    return Result{res};
}

Result Connection::exec_params(const std::string& sql, const std::vector<std::string>& params) {
    std::vector<const char*> values;
    values.reserve(params.size());
    for (const auto& p : params) values.push_back(p.c_str());

    PGresult* res = PQexecParams(
        conn_, sql.c_str(), static_cast<int>(values.size()),
        nullptr, values.data(), nullptr, nullptr, 0);

    auto status = PQresultStatus(res);
    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
        std::string msg = PQresultErrorMessage(res);
        PQclear(res);
        throw QueryError("query failed: " + msg);
    }
    return Result{res};
}

} // namespace game::db
