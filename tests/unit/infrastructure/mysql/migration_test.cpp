#include "infrastructure/mysql/migration.hpp"

#include <cassert>
#include <filesystem>

namespace mysql = warehouse::infrastructure::mysql;

int main(int argc, char** argv) {
    assert(mysql::isSingleServerStatement("SELECT ';' AS value;"));
    assert(mysql::isSingleServerStatement("/* ; */ CREATE TABLE x(id INT);"));
    assert(!mysql::isSingleServerStatement("SELECT 1; SELECT 2;"));

    const std::filesystem::path root = argc > 1 ? argv[1] : "migrations";
    auto migrations = mysql::discoverMigrations(root);
    assert(migrations);
    assert(migrations.value().size() == 1);
    assert(migrations.value().front().version == 1);
    assert(migrations.value().front().steps.size() == 2);
    for (const auto& step : migrations.value().front().steps) {
        assert(step.phase != mysql::MigrationPhase::Cleanup);
        assert(mysql::isSingleServerStatement(step.applySql));
        assert(mysql::isSingleServerStatement(step.verifySql));
        assert(step.checksum == mysql::migrationChecksum(
                                    step.id, step.phase, step.execution,
                                    step.applySql, step.verifySql));
    }
    auto bootstrap = mysql::readMigrationBootstrapSql(root);
    assert(bootstrap);
    assert(bootstrap.value().size() == 2);
    for (const auto& step : bootstrap.value()) {
        assert(!step.id.empty());
        assert(mysql::isSingleServerStatement(step.applySql));
        assert(mysql::isSingleServerStatement(step.verifySql));
    }
}
