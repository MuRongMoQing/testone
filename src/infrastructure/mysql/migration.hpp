#pragma once

#include "application/common/result.hpp"
#include "infrastructure/mysql/executor.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace warehouse::infrastructure::mysql {

enum class MigrationPhase { Precheck, Expand, Backfill, Switch, Cleanup };
enum class MigrationExecution { Check, AtomicDdl, TransactionalDml };

struct MigrationStep {
    std::string id;
    MigrationPhase phase = MigrationPhase::Precheck;
    MigrationExecution execution = MigrationExecution::Check;
    std::string applySql;
    std::string verifySql;
    std::string checksum;
};

struct MigrationDefinition {
    unsigned int version = 0;
    std::string name;
    std::vector<MigrationStep> steps;
};

struct MigrationError {
    std::string message;
};

struct MigrationBootstrapStep {
    std::string id;
    std::string applySql;
    std::string verifySql;
};

using MigrationDefinitions =
    application::Result<std::vector<MigrationDefinition>, MigrationError>;
using MigrationBootstrapSql =
    application::Result<std::vector<MigrationBootstrapStep>, MigrationError>;

bool isSingleServerStatement(std::string_view sql) noexcept;
std::string migrationChecksum(std::string_view stepId, MigrationPhase phase,
                              MigrationExecution execution,
                              std::string_view applySql, std::string_view verifySql);
MigrationDefinitions discoverMigrations(const std::filesystem::path& root);
MigrationBootstrapSql readMigrationBootstrapSql(const std::filesystem::path& root);

enum class MigrationRunStatus { Succeeded, Failed };
struct MigrationReport {
    MigrationRunStatus status = MigrationRunStatus::Failed;
    unsigned int versionsApplied = 0;
    unsigned int versionsAlreadyComplete = 0;
    unsigned int stepsApplied = 0;
    std::vector<std::string> appliedSteps;
    std::vector<std::string> alreadyCompleteSteps;
    std::vector<std::string> pendingSteps;
    std::vector<std::string> deferredCleanupSteps;
    std::vector<std::string> nonRollbackableSteps;
    std::string failedStep;
    std::string message;
};

class MigrationRunner final {
public:
    explicit MigrationRunner(SqlExecutor& executor) : executor_(executor) {}
    MigrationReport run(const std::vector<MigrationDefinition>& definitions,
                        const std::vector<MigrationBootstrapStep>& bootstrapSql,
                        unsigned int lockTimeoutSeconds = 10);

private:
    SqlExecutor& executor_;
};

}  // namespace warehouse::infrastructure::mysql
