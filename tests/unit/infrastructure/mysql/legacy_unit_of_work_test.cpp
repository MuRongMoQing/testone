#include "infrastructure/mysql/legacy_unit_of_work.hpp"

#include <cassert>
#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace mysql = warehouse::infrastructure::mysql;

class RecordingExecutor final : public mysql::SqlExecutor {
public:
    mysql::SqlResult executeTrusted(std::string_view sql) override {
        trusted.emplace_back(sql);
        return mysql::SqlResult::success({});
    }

    mysql::SqlResult executePrepared(
        std::string_view sql, const std::vector<mysql::SqlValue>& parameters) override {
        prepared.emplace_back(sql);
        bound.push_back(parameters);
        if (sql.find("SELECT id,name,location,status") == 0) {
            return mysql::SqlResult::success(
                {{{std::string("7"), std::string("display"), std::string("A-01"),
                   std::string("stored"), std::string("2026-07-19 10:00:00"),
                   std::nullopt, std::string("manager")}}, 0, 0});
        }
        if (sql.find("INSERT INTO goods") == 0) {
            mysql::SqlResponse response;
            response.affectedRows = 1;
            response.lastInsertId = 7;
            return mysql::SqlResult::success(std::move(response));
        }
        return mysql::SqlResult::success({});
    }

    bool reusable() const noexcept override { return !poisoned; }
    void poison() noexcept override { poisoned = true; }

    std::vector<std::string> trusted;
    std::vector<std::string> prepared;
    std::vector<std::vector<mysql::SqlValue>> bound;
    bool poisoned = false;
};

int main() {
    RecordingExecutor* recording = nullptr;
    mysql::ConnectionPool pool(1, [&] {
        auto concrete = std::make_unique<RecordingExecutor>();
        recording = concrete.get();
        std::unique_ptr<mysql::SqlExecutor> executor = std::move(concrete);
        return warehouse::application::Result<std::unique_ptr<mysql::SqlExecutor>,
                                              mysql::SqlError>::success(std::move(executor));
    });
    mysql::MySqlLegacyUnitOfWorkFactory factory(pool, std::chrono::milliseconds(10));

    auto unit = factory.begin(warehouse::application::WorkUnitMode::ShortRead);
    assert(unit);
    const std::string hostile = "x' OR 1=1 --";
    auto rows = unit.value()->goods().list({hostile, "stored"});
    assert(rows && rows.value().size() == 1);
    assert(recording != nullptr);
    assert(recording->prepared.front().find(hostile) == std::string::npos);
    assert(recording->prepared.front().find('?') != std::string::npos);
    assert(recording->prepared.front().find("ORDER BY id") != std::string::npos);
    assert(recording->prepared.front().find("DESC") == std::string::npos);
    assert(std::get<std::string>(recording->bound.front().at(0)) == hostile);
    assert(unit.value()->commit());
    unit.value().reset();

    auto command = factory.begin(warehouse::application::WorkUnitMode::Command);
    assert(command);
    auto created = command.value()->goods().create({"keyboard", "B-01", "manager"});
    assert(created && created.value().id == 7);
    for (const auto& sql : recording->prepared) {
        assert(sql.find("DELETE FROM goods") == std::string::npos);
    }
    command.value()->rollback();
}
