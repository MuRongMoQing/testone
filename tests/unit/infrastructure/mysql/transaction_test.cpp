#include "infrastructure/mysql/transaction.hpp"

#include <cassert>
#include <string>
#include <vector>

namespace mysql = warehouse::infrastructure::mysql;

class RecordingExecutor final : public mysql::SqlExecutor {
public:
    mysql::SqlResult executeTrusted(std::string_view sql) override {
        statements.emplace_back(sql);
        if (failCommit && sql == "COMMIT") {
            return mysql::SqlResult::failure(
                {mysql::SqlErrorCode::ConnectionLost, 2013, "lost during commit"});
        }
        return mysql::SqlResult::success({});
    }
    mysql::SqlResult executePrepared(
        std::string_view, const std::vector<mysql::SqlValue>&) override {
        return mysql::SqlResult::success({});
    }
    bool reusable() const noexcept override { return !poisoned; }
    void poison() noexcept override { poisoned = true; }

    std::vector<std::string> statements;
    bool failCommit = false;
    bool poisoned = false;
};

int main() {
    RecordingExecutor command;
    auto commandTx = mysql::Transaction::begin(
        command, warehouse::application::WorkUnitMode::Command);
    assert(commandTx);
    assert((command.statements == std::vector<std::string>{
        "SET TRANSACTION ISOLATION LEVEL READ COMMITTED", "START TRANSACTION"}));
    assert(commandTx.value().commit());

    RecordingExecutor shortRead;
    auto shortTx = mysql::Transaction::begin(
        shortRead, warehouse::application::WorkUnitMode::ShortRead);
    assert(shortTx);
    assert((shortRead.statements == std::vector<std::string>{
        "SET TRANSACTION ISOLATION LEVEL READ COMMITTED",
        "START TRANSACTION READ ONLY"}));

    RecordingExecutor finalized;
    auto finalizedTx = mysql::Transaction::begin(
        finalized, warehouse::application::WorkUnitMode::FinalizedRead);
    assert(finalizedTx);
    assert((finalized.statements == std::vector<std::string>{
        "SET TRANSACTION ISOLATION LEVEL REPEATABLE READ",
        "START TRANSACTION WITH CONSISTENT SNAPSHOT, READ ONLY"}));

    RecordingExecutor uncertain;
    uncertain.failCommit = true;
    auto uncertainTx = mysql::Transaction::begin(
        uncertain, warehouse::application::WorkUnitMode::Command);
    assert(uncertainTx);
    auto committed = uncertainTx.value().commit();
    assert(!committed);
    assert(committed.error().code ==
        warehouse::application::WorkUnitFailureCode::CommitOutcomeUnknown);
    assert(uncertain.poisoned);
}
