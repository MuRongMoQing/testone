#include "infrastructure/mysql/migration.hpp"

#include <algorithm>
#include <cassert>
#include <string>
#include <vector>

namespace mysql = warehouse::infrastructure::mysql;

class MigrationExecutor final : public mysql::SqlExecutor {
public:
    mysql::SqlResult executeTrusted(std::string_view sql) override {
        trusted.emplace_back(sql);
        if (failApply && sql.find("CREATE TABLE users") == 0) {
            return mysql::SqlResult::failure(
                {mysql::SqlErrorCode::Statement, 1064, "forced apply failure"});
        }
        if (sql.find("SELECT COUNT(*)") == 0) {
            return mysql::SqlResult::success({{{std::string("1")}}, 0, 0});
        }
        return mysql::SqlResult::success({});
    }

    mysql::SqlResult executePrepared(
        std::string_view sql, const std::vector<mysql::SqlValue>& parameters) override {
        prepared.emplace_back(sql);
        if (failFailureStateWrite &&
            sql.find("INSERT INTO schema_migration_steps") == 0 &&
            parameters.size() > 5 &&
            std::get<std::string>(parameters[5]) == "FAILED") {
            return mysql::SqlResult::failure(
                {mysql::SqlErrorCode::ConnectionLost, 2013,
                 "forced failure-state write failure"});
        }
        if (sql.find("SELECT GET_LOCK") == 0 || sql.find("SELECT RELEASE_LOCK") == 0) {
            return mysql::SqlResult::success({{{std::string("1")}}, 0, 0});
        }
        if (sql.find("SELECT step_id,checksum,phase,execution,status") == 0) {
            if (returnChecksumMismatch) {
                return mysql::SqlResult::success(
                    {{{std::string("001_users"), std::string(64, '0'),
                       std::string("expand"), std::string("atomic_ddl"),
                       std::string("SUCCEEDED")}}, 0, 0});
            }
            if (returnIncompleteStep) {
                return mysql::SqlResult::success(
                    {{{std::string("001_users"), expectedChecksum,
                       std::string("expand"), std::string("atomic_ddl"),
                       std::string("RUNNING")}}, 0, 0});
            }
            if (returnUnknownStep) {
                return mysql::SqlResult::success(
                    {{{std::string("removed_step"), expectedChecksum,
                       std::string("expand"), std::string("atomic_ddl"),
                       std::string("SUCCEEDED")}}, 0, 0});
            }
            if (returnPhaseMismatch) {
                return mysql::SqlResult::success(
                    {{{std::string("001_users"), expectedChecksum,
                       std::string("cleanup"), std::string("atomic_ddl"),
                       std::string("SUCCEEDED")}}, 0, 0});
            }
            return mysql::SqlResult::success({});
        }
        if (sql.find("SELECT name,status") == 0) {
            if (returnCompleteVersion) {
                return mysql::SqlResult::success(
                    {{{std::string("baseline"), std::string("SUCCEEDED")}}, 0, 0});
            }
            return mysql::SqlResult::success({});
        }
        return mysql::SqlResult::success({});
    }

    bool reusable() const noexcept override { return !poisoned; }
    void poison() noexcept override { poisoned = true; }

    std::vector<std::string> trusted;
    std::vector<std::string> prepared;
    bool returnChecksumMismatch = false;
    bool returnCompleteVersion = false;
    bool returnIncompleteStep = false;
    bool returnUnknownStep = false;
    bool returnPhaseMismatch = false;
    bool failApply = false;
    bool failFailureStateWrite = false;
    std::string expectedChecksum;
    bool poisoned = false;
};

int main() {
    const mysql::MigrationStep step{
        "001_users", mysql::MigrationPhase::Expand,
        mysql::MigrationExecution::AtomicDdl,
        "CREATE TABLE users(id INT);", "SELECT COUNT(*) FROM users;",
        mysql::migrationChecksum("001_users", mysql::MigrationPhase::Expand,
                                 mysql::MigrationExecution::AtomicDdl,
                                 "CREATE TABLE users(id INT);",
                                 "SELECT COUNT(*) FROM users;")};
    const std::vector<mysql::MigrationDefinition> definitions{{1, "baseline", {step}}};

    MigrationExecutor executor;
    mysql::MigrationRunner runner(executor);
    const std::vector<mysql::MigrationBootstrapStep> bootstrapSql{
        {"schema_migrations",
         "CREATE TABLE IF NOT EXISTS schema_migrations(version INT);",
         "SELECT COUNT(*) FROM metadata;"},
        {"schema_migration_steps",
         "CREATE TABLE IF NOT EXISTS schema_migration_steps(version INT);",
         "SELECT COUNT(*) FROM metadata;"}};
    const auto report = runner.run(definitions, bootstrapSql, 1);
    assert(report.status == mysql::MigrationRunStatus::Succeeded);
    assert(report.versionsApplied == 1);
    assert(report.stepsApplied == 1);
    assert(report.appliedSteps == std::vector<std::string>{"001_users"});
    assert(report.pendingSteps.empty());
    assert(report.nonRollbackableSteps == std::vector<std::string>{"001_users"});
    assert(executor.trusted.size() >= 6);
    assert(std::any_of(executor.prepared.begin(), executor.prepared.end(),
                       [](const std::string& sql) {
                           return sql.find("INSERT INTO schema_migrations") == 0;
                       }));

    MigrationExecutor mismatch;
    mismatch.returnChecksumMismatch = true;
    mysql::MigrationRunner mismatchRunner(mismatch);
    const auto rejected = mismatchRunner.run(definitions, bootstrapSql, 1);
    assert(rejected.status == mysql::MigrationRunStatus::Failed);
    assert(rejected.failedStep == "001_users");
    assert(rejected.message.find("metadata changed") != std::string::npos);

    MigrationExecutor contradictory;
    contradictory.returnCompleteVersion = true;
    contradictory.returnIncompleteStep = true;
    contradictory.expectedChecksum = step.checksum;
    mysql::MigrationRunner contradictoryRunner(contradictory);
    const auto contradiction =
        contradictoryRunner.run(definitions, bootstrapSql, 1);
    assert(contradiction.status == mysql::MigrationRunStatus::Failed);
    assert(contradiction.failedStep == "001_users");
    assert(contradiction.message.find("incomplete step") != std::string::npos);
    assert(contradiction.versionsAlreadyComplete == 0);

    MigrationExecutor removed;
    removed.returnUnknownStep = true;
    removed.expectedChecksum = step.checksum;
    mysql::MigrationRunner removedRunner(removed);
    const auto missingManifestStep = removedRunner.run(definitions, bootstrapSql, 1);
    assert(missingManifestStep.status == mysql::MigrationRunStatus::Failed);
    assert(missingManifestStep.failedStep == "removed_step");
    assert(missingManifestStep.message.find("missing from") != std::string::npos);

    MigrationExecutor changedPhase;
    changedPhase.returnPhaseMismatch = true;
    changedPhase.expectedChecksum = step.checksum;
    mysql::MigrationRunner changedPhaseRunner(changedPhase);
    const auto changedPhaseReport =
        changedPhaseRunner.run(definitions, bootstrapSql, 1);
    assert(changedPhaseReport.status == mysql::MigrationRunStatus::Failed);
    assert(changedPhaseReport.message.find("metadata changed") != std::string::npos);

    MigrationExecutor failedState;
    failedState.failApply = true;
    failedState.failFailureStateWrite = true;
    mysql::MigrationRunner failedStateRunner(failedState);
    const auto failedStateReport =
        failedStateRunner.run(definitions, bootstrapSql, 1);
    assert(failedStateReport.status == mysql::MigrationRunStatus::Failed);
    assert(failedStateReport.message.find("additionally failed to record the step") !=
           std::string::npos);
    assert(failedState.poisoned);
}
