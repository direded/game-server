#include "world/character_store.h"

#include "db/connection.h"

#include <charconv>
#include <stdexcept>

namespace game::world {

namespace {

template <typename T>
T parse_int(std::string_view s) {
    long long v = 0;
    std::from_chars(s.data(), s.data() + s.size(), v);
    return static_cast<T>(v);
}

Character row_to_character(std::string_view id, std::string_view account_id,
                           std::string_view name, std::string_view location_id) {
    Character c;
    c.id = parse_int<CharacterId>(id);
    c.account_id = parse_int<AccountId>(account_id);
    c.name = std::string(name);
    c.location_id = parse_int<LocationId>(location_id);
    return c;
}

} // namespace

bool is_valid_character_name(std::string_view name) {
    if (name.size() < 3 || name.size() > 24) return false;
    // First char must be a letter.
    const char f = name[0];
    const bool first_ok = (f >= 'A' && f <= 'Z') || (f >= 'a' && f <= 'z');
    if (!first_ok) return false;
    // Remaining chars: letters, digits, underscore, hyphen, or space.
    for (size_t i = 1; i < name.size(); ++i) {
        const char c = name[i];
        const bool ok = (c >= 'A' && c <= 'Z')
                     || (c >= 'a' && c <= 'z')
                     || (c >= '0' && c <= '9')
                     || c == '_' || c == '-' || c == ' ';
        if (!ok) return false;
    }
    return true;
}

CharacterStore::CharacterStore(db::Connection& conn) : conn_(conn) {}

Character CharacterStore::create(AccountId account_id,
                                 std::string_view name,
                                 LocationId spawn_location_id) {
    std::lock_guard<std::mutex> lg(mu_);

    const std::string sql =
        "INSERT INTO characters (account_id, name, location_id) "
        "VALUES ($1::bigint, $2::citext, $3::int) "
        "RETURNING id";
    auto res = conn_.exec_params(sql, {
        std::to_string(account_id),
        std::string(name),
        std::to_string(spawn_location_id),
    });

    Character c;
    c.id = parse_int<CharacterId>(res.at(0, 0));
    c.account_id = account_id;
    c.name = std::string(name);
    c.location_id = spawn_location_id;
    return c;
}

std::vector<Character> CharacterStore::load_all() {
    std::lock_guard<std::mutex> lg(mu_);

    auto res = conn_.exec(
        "SELECT id, account_id, name, location_id FROM characters");
    std::vector<Character> out;
    out.reserve(static_cast<size_t>(res.rows()));
    for (int i = 0; i < res.rows(); ++i) {
        out.push_back(row_to_character(res.at(i, 0), res.at(i, 1),
                                       res.at(i, 2), res.at(i, 3)));
    }
    return out;
}

std::optional<Character> CharacterStore::find_by_id(CharacterId id) {
    std::lock_guard<std::mutex> lg(mu_);

    auto res = conn_.exec_params(
        "SELECT id, account_id, name, location_id FROM characters "
        "WHERE id = $1::bigint",
        {std::to_string(id)});
    if (res.rows() == 0) return std::nullopt;
    return row_to_character(res.at(0, 0), res.at(0, 1),
                            res.at(0, 2), res.at(0, 3));
}

std::vector<Character> CharacterStore::list_by_account(AccountId account_id) {
    std::lock_guard<std::mutex> lg(mu_);

    auto res = conn_.exec_params(
        "SELECT id, account_id, name, location_id FROM characters "
        "WHERE account_id = $1::bigint ORDER BY id",
        {std::to_string(account_id)});
    std::vector<Character> out;
    out.reserve(static_cast<size_t>(res.rows()));
    for (int i = 0; i < res.rows(); ++i) {
        out.push_back(row_to_character(res.at(i, 0), res.at(i, 1),
                                       res.at(i, 2), res.at(i, 3)));
    }
    return out;
}

} // namespace game::world
