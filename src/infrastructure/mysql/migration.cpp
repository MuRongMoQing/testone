#include "infrastructure/mysql/migration.hpp"
#include "infrastructure/mysql/sha256.hpp"
#include "infrastructure/mysql/transaction.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <sstream>

namespace warehouse::infrastructure::mysql {
namespace {

std::string trim(std::string value) {
    const auto nonSpace = [](unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), nonSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), nonSpace).base(), value.end());
    return value;
}

application::Result<std::string, MigrationError> readText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return application::Result<std::string, MigrationError>::failure(
        {"cannot read " + path.generic_string()});
    std::ostringstream contents;
    contents << input.rdbuf();
    std::string normalized = contents.str();
    normalized.erase(std::remove(normalized.begin(), normalized.end(), '\r'), normalized.end());
    return application::Result<std::string, MigrationError>::success(std::move(normalized));
}

application::Result<MigrationPhase, MigrationError> phase(std::string value) {
    if (value == "precheck") return decltype(phase(value))::success(MigrationPhase::Precheck);
    if (value == "expand") return decltype(phase(value))::success(MigrationPhase::Expand);
    if (value == "backfill") return decltype(phase(value))::success(MigrationPhase::Backfill);
    if (value == "switch") return decltype(phase(value))::success(MigrationPhase::Switch);
    if (value == "cleanup") return decltype(phase(value))::success(MigrationPhase::Cleanup);
    return decltype(phase(value))::failure({"unknown migration phase: " + value});
}

application::Result<MigrationExecution, MigrationError> execution(std::string value) {
    if (value == "check") return decltype(execution(value))::success(MigrationExecution::Check);
    if (value == "atomic_ddl") return decltype(execution(value))::success(MigrationExecution::AtomicDdl);
    if (value == "transactional_dml") return decltype(execution(value))::success(MigrationExecution::TransactionalDml);
    return decltype(execution(value))::failure({"unknown migration execution: " + value});
}

application::Result<MigrationStep, MigrationError> readStep(
    const std::filesystem::path& directory, const std::filesystem::path& manifest) {
    auto text = readText(manifest);
    if (!text) return application::Result<MigrationStep, MigrationError>::failure(text.error());
    std::map<std::string, std::string> fields;
    std::istringstream lines(text.value());
    for (std::string line; std::getline(lines, line);) {
        line = trim(line);
        if (line.empty() || line.front() == '#') continue;
        const auto equals = line.find('=');
        if (equals == std::string::npos) return application::Result<MigrationStep, MigrationError>::failure(
            {"invalid manifest line in " + manifest.generic_string()});
        fields.emplace(trim(line.substr(0, equals)), trim(line.substr(equals + 1)));
    }
    for (const char* required : {"id", "phase", "execution", "apply", "verify", "sha256"}) {
        if (fields[required].empty()) return application::Result<MigrationStep, MigrationError>::failure(
            {std::string("missing ") + required + " in " + manifest.generic_string()});
    }
    auto parsedPhase = phase(fields["phase"]);
    auto parsedExecution = execution(fields["execution"]);
    if (!parsedPhase) return application::Result<MigrationStep, MigrationError>::failure(parsedPhase.error());
    if (!parsedExecution) return application::Result<MigrationStep, MigrationError>::failure(parsedExecution.error());
    auto apply = readText(directory / fields["apply"]);
    auto verify = readText(directory / fields["verify"]);
    if (!apply) return application::Result<MigrationStep, MigrationError>::failure(apply.error());
    if (!verify) return application::Result<MigrationStep, MigrationError>::failure(verify.error());
    if (!isSingleServerStatement(apply.value()) || !isSingleServerStatement(verify.value())) {
        return application::Result<MigrationStep, MigrationError>::failure(
            {"migration apply and verify files must contain one server statement"});
    }
    const auto actual = migrationChecksum(fields["id"], parsedPhase.value(),
                                          parsedExecution.value(), apply.value(),
                                          verify.value());
    if (actual != fields["sha256"]) return application::Result<MigrationStep, MigrationError>::failure(
        {"migration checksum mismatch for " + fields["id"]});
    return application::Result<MigrationStep, MigrationError>::success(
        {fields["id"], parsedPhase.value(), parsedExecution.value(),
         std::move(apply.value()), std::move(verify.value()), actual});
}

}  // namespace

bool isSingleServerStatement(std::string_view sql) noexcept {
    enum class State { Code, SingleQuote, DoubleQuote, Backtick, LineComment, BlockComment };
    State state = State::Code;
    bool ended = false;
    bool sawCode = false;
    for (std::size_t i = 0; i < sql.size(); ++i) {
        const char c = sql[i];
        const char next = i + 1 < sql.size() ? sql[i + 1] : '\0';
        if (state == State::LineComment) { if (c == '\n') state = State::Code; continue; }
        if (state == State::BlockComment) { if (c == '*' && next == '/') { state = State::Code; ++i; } continue; }
        if (state == State::SingleQuote || state == State::DoubleQuote || state == State::Backtick) {
            const char delimiter = state == State::SingleQuote ? '\'' : state == State::DoubleQuote ? '"' : '`';
            if (c == '\\') { ++i; continue; }
            if (c == delimiter) { if (next == delimiter) ++i; else state = State::Code; }
            continue;
        }
        if (c == '#' || (c == '-' && next == '-' && i + 2 < sql.size() && std::isspace(static_cast<unsigned char>(sql[i + 2])))) {
            state = State::LineComment; if (c == '-') ++i; continue;
        }
        if (c == '/' && next == '*') { state = State::BlockComment; ++i; continue; }
        if (c == '\'') { state = State::SingleQuote; continue; }
        if (c == '"') { state = State::DoubleQuote; continue; }
        if (c == '`') { state = State::Backtick; continue; }
        if (c == ';') { ended = true; continue; }
        if (ended && !std::isspace(static_cast<unsigned char>(c))) return false;
        if (!std::isspace(static_cast<unsigned char>(c))) sawCode = true;
    }
    return sawCode && (state == State::Code || state == State::LineComment);
}

std::string migrationChecksum(std::string_view stepId, MigrationPhase phase,
                              MigrationExecution execution,
                              std::string_view applySql, std::string_view verifySql) {
    std::string material;
    const auto append = [&material](std::string_view value) {
        material += std::to_string(value.size());
        material.push_back(':');
        material.append(value);
    };
    const char* phaseValue = phase == MigrationPhase::Precheck ? "precheck"
        : phase == MigrationPhase::Expand ? "expand"
        : phase == MigrationPhase::Backfill ? "backfill"
        : phase == MigrationPhase::Switch ? "switch"
                                          : "cleanup";
    const char* executionValue = execution == MigrationExecution::Check ? "check"
        : execution == MigrationExecution::AtomicDdl ? "atomic_ddl"
                                                     : "transactional_dml";
    append(stepId);
    append(phaseValue);
    append(executionValue);
    append(applySql);
    append(verifySql);
    return sha256Hex(material);
}

MigrationDefinitions discoverMigrations(const std::filesystem::path& root) {
    std::vector<MigrationDefinition> definitions;
    if (!std::filesystem::is_directory(root)) return MigrationDefinitions::failure(
        {"migration root is not a directory: " + root.generic_string()});
    const std::regex directoryName(R"((\d{4})_([a-z0-9_]+))");
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        if (!entry.is_directory()) continue;
        std::smatch match;
        const auto name = entry.path().filename().string();
        if (!std::regex_match(name, match, directoryName)) return MigrationDefinitions::failure(
            {"invalid migration directory: " + name});
        MigrationDefinition definition;
        definition.version = static_cast<unsigned int>(std::stoul(match[1].str()));
        definition.name = match[2].str();
        for (const auto& file : std::filesystem::directory_iterator(entry.path())) {
            if (file.path().extension() != ".step") continue;
            auto step = readStep(entry.path(), file.path());
            if (!step) return MigrationDefinitions::failure(step.error());
            definition.steps.push_back(std::move(step.value()));
        }
        std::sort(definition.steps.begin(), definition.steps.end(),
                  [](const auto& left, const auto& right) { return left.id < right.id; });
        if (definition.steps.empty()) return MigrationDefinitions::failure(
            {"migration has no steps: " + name});
        for (std::size_t index = 1; index < definition.steps.size(); ++index) {
            if (definition.steps[index - 1].id == definition.steps[index].id) {
                return MigrationDefinitions::failure(
                    {"duplicate migration step id: " + definition.steps[index].id});
            }
        }
        definitions.push_back(std::move(definition));
    }
    std::sort(definitions.begin(), definitions.end(),
              [](const auto& left, const auto& right) { return left.version < right.version; });
    for (std::size_t i = 1; i < definitions.size(); ++i) {
        if (definitions[i - 1].version == definitions[i].version) return MigrationDefinitions::failure(
            {"duplicate migration version"});
    }
    return MigrationDefinitions::success(std::move(definitions));
}

MigrationBootstrapSql readMigrationBootstrapSql(const std::filesystem::path& root) {
    static constexpr const char* names[] = {
        "schema_migrations",
        "schema_migration_steps",
    };
    std::vector<MigrationBootstrapStep> steps;
    steps.reserve(std::size(names));
    for (const auto* name : names) {
        const std::string applyFile = std::string(name) + ".bootstrap.sql";
        const std::string verifyFile = std::string(name) + ".bootstrap.verify.sql";
        auto apply = readText(root / applyFile);
        auto verify = readText(root / verifyFile);
        if (!apply) return MigrationBootstrapSql::failure(apply.error());
        if (!verify) return MigrationBootstrapSql::failure(verify.error());
        if (!isSingleServerStatement(apply.value()) ||
            !isSingleServerStatement(verify.value())) {
            return MigrationBootstrapSql::failure(
                {"migration bootstrap apply and verify files must contain one server statement: " +
                 std::string(name)});
        }
        steps.push_back({name, std::move(apply.value()), std::move(verify.value())});
    }
    return MigrationBootstrapSql::success(std::move(steps));
}

namespace {

bool truthy(const SqlResponse& response) {
    if (response.rows.size() != 1 || response.rows.front().size() != 1 ||
        !response.rows.front().front()) return false;
    const auto& value = *response.rows.front().front();
    return value == "1" || value == "true" || value == "TRUE";
}

std::string phaseName(MigrationPhase value) {
    switch (value) {
    case MigrationPhase::Precheck: return "precheck";
    case MigrationPhase::Expand: return "expand";
    case MigrationPhase::Backfill: return "backfill";
    case MigrationPhase::Switch: return "switch";
    case MigrationPhase::Cleanup: return "cleanup";
    }
    return "unknown";
}

std::string executionName(MigrationExecution value) {
    switch (value) {
    case MigrationExecution::Check: return "check";
    case MigrationExecution::AtomicDdl: return "atomic_ddl";
    case MigrationExecution::TransactionalDml: return "transactional_dml";
    }
    return "unknown";
}

class MigrationLock final {
public:
    explicit MigrationLock(SqlExecutor& executor) : executor_(executor) {}
    ~MigrationLock() {
        if (!held_) return;
        auto released = executor_.executePrepared("SELECT RELEASE_LOCK(?)", {lockName()});
        if (!released || !truthy(released.value())) executor_.poison();
    }
    void held() noexcept { held_ = true; }
    bool release() {
        if (!held_) return true;
        auto released = executor_.executePrepared("SELECT RELEASE_LOCK(?)", {lockName()});
        held_ = false;
        if (!released || !truthy(released.value())) {
            executor_.poison();
            return false;
        }
        return true;
    }
    static std::string lockName() { return "warehouse_schema_migrations"; }

private:
    SqlExecutor& executor_;
    bool held_ = false;
};

MigrationReport failed(MigrationReport report, std::string step, std::string message) {
    report.status = MigrationRunStatus::Failed;
    report.failedStep = std::move(step);
    report.message = std::move(message);
    return report;
}

void removePending(MigrationReport& report, const std::string& stepId) {
    report.pendingSteps.erase(
        std::remove(report.pendingSteps.begin(), report.pendingSteps.end(), stepId),
        report.pendingSteps.end());
}

bool verifyStep(SqlExecutor& executor, const MigrationStep& step, std::string& error) {
    auto verified = executor.executeTrusted(step.verifySql);
    if (!verified) {
        error = "verification query failed: " + verified.error().message;
        return false;
    }
    if (!truthy(verified.value())) {
        error = "verification did not confirm the target state";
        return false;
    }
    return true;
}

bool updateStepStatus(SqlExecutor& executor, unsigned int version,
                      const MigrationStep& step, std::string_view status,
                      std::string_view errorMessage) {
    auto result = executor.executePrepared(
        "INSERT INTO schema_migration_steps"
        "(version,step_id,checksum,phase,execution,status,started_at,finished_at,error_message) "
        "VALUES(?,?,?,?,?,?,UTC_TIMESTAMP(),"
        "CASE WHEN ?='SUCCEEDED' THEN UTC_TIMESTAMP() ELSE NULL END,?) "
        "ON DUPLICATE KEY UPDATE status=VALUES(status),"
        "started_at=CASE WHEN VALUES(status)='RUNNING' THEN UTC_TIMESTAMP() ELSE started_at END,"
        "finished_at=CASE WHEN VALUES(status)='SUCCEEDED' THEN UTC_TIMESTAMP() ELSE NULL END,"
        "error_message=VALUES(error_message)",
        {static_cast<std::uint64_t>(version), step.id, step.checksum,
         phaseName(step.phase), executionName(step.execution), std::string(status),
         std::string(status), std::string(errorMessage)});
    return static_cast<bool>(result);
}

bool updateVersionStatus(SqlExecutor& executor, const MigrationDefinition& definition,
                         std::string_view status, std::string_view errorMessage) {
    auto result = executor.executePrepared(
        "INSERT INTO schema_migrations"
        "(version,name,status,started_at,finished_at,error_message) "
        "VALUES(?,?,?,UTC_TIMESTAMP(),"
        "CASE WHEN ?='SUCCEEDED' THEN UTC_TIMESTAMP() ELSE NULL END,?) "
        "ON DUPLICATE KEY UPDATE name=VALUES(name),status=VALUES(status),"
        "started_at=CASE WHEN VALUES(status)='RUNNING' THEN UTC_TIMESTAMP() ELSE started_at END,"
        "finished_at=CASE WHEN VALUES(status)='SUCCEEDED' THEN UTC_TIMESTAMP() ELSE NULL END,"
        "error_message=VALUES(error_message)",
        {static_cast<std::uint64_t>(definition.version), definition.name,
         std::string(status), std::string(status), std::string(errorMessage)});
    return static_cast<bool>(result);
}

}  // namespace

MigrationReport MigrationRunner::run(const std::vector<MigrationDefinition>& definitions,
                                     const std::vector<MigrationBootstrapStep>& bootstrapSql,
                                     unsigned int lockTimeoutSeconds) {
    MigrationReport report;
    for (const auto& definition : definitions) {
        for (const auto& step : definition.steps) {
            if (step.phase == MigrationPhase::Cleanup) {
                report.deferredCleanupSteps.push_back(step.id);
            } else {
                report.pendingSteps.push_back(step.id);
            }
        }
    }
    MigrationLock lock(executor_);
    auto acquired = executor_.executePrepared(
        "SELECT GET_LOCK(?,?)",
        {MigrationLock::lockName(), static_cast<std::uint64_t>(lockTimeoutSeconds)});
    if (!acquired || !truthy(acquired.value())) {
        return failed(std::move(report), "", acquired ? "migration lock was not acquired"
                                    : "migration lock failed: " + acquired.error().message);
    }
    lock.held();

    if (bootstrapSql.empty()) {
        return failed(std::move(report), "", "migration bootstrap SQL is missing");
    }
    for (const auto& bootstrap : bootstrapSql) {
        if (!isSingleServerStatement(bootstrap.applySql) ||
            !isSingleServerStatement(bootstrap.verifySql)) {
            return failed(std::move(report), "",
                          "migration bootstrap SQL contains multiple statements");
        }
        auto metadata = executor_.executeTrusted(bootstrap.applySql);
        if (!metadata) {
            return failed(std::move(report), "",
                          "migration metadata table failed: " + metadata.error().message);
        }
        auto verified = executor_.executeTrusted(bootstrap.verifySql);
        if (!verified || !truthy(verified.value())) {
            return failed(std::move(report), bootstrap.id,
                          verified ? "migration metadata structure is incompatible"
                                   : "migration metadata verification failed: " +
                                         verified.error().message);
        }
    }

    report.status = MigrationRunStatus::Succeeded;
    for (const auto& definition : definitions) {
        auto versionState = executor_.executePrepared(
            "SELECT name,status FROM schema_migrations WHERE version=?",
            {static_cast<std::uint64_t>(definition.version)});
        if (!versionState) {
            return failed(std::move(report), "", "migration version state read failed: " +
                                                   versionState.error().message);
        }
        bool versionWasComplete = false;
        if (!versionState.value().rows.empty()) {
            const auto& row = versionState.value().rows.front();
            if (row.size() != 2 || !row[0] || !row[1]) {
                return failed(std::move(report), "", "migration version state row is invalid");
            }
            if (*row[0] != definition.name) {
                return failed(std::move(report), "", "migration version name changed");
            }
            versionWasComplete = *row[1] == "SUCCEEDED";
        }
        std::map<std::string, const MigrationStep*> currentSteps;
        for (const auto& step : definition.steps) currentSteps.emplace(step.id, &step);
        auto recorded = executor_.executePrepared(
            "SELECT step_id,checksum,phase,execution,status "
            "FROM schema_migration_steps WHERE version=? ORDER BY step_id",
            {static_cast<std::uint64_t>(definition.version)});
        if (!recorded) {
            return failed(std::move(report), "", "migration manifest state read failed: " +
                                                   recorded.error().message);
        }
        std::map<std::string, std::array<std::string, 4>> recordedSteps;
        for (const auto& row : recorded.value().rows) {
            if (row.size() != 5 || !row[0] || !row[1] || !row[2] || !row[3] || !row[4]) {
                return failed(std::move(report), "", "migration manifest row is invalid");
            }
            const auto current = currentSteps.find(*row[0]);
            if (current == currentSteps.end()) {
                return failed(std::move(report), *row[0],
                              "recorded migration step is missing from the versioned manifest");
            }
            const auto& step = *current->second;
            if (*row[1] != step.checksum || *row[2] != phaseName(step.phase) ||
                *row[3] != executionName(step.execution)) {
                return failed(std::move(report), step.id,
                              "recorded migration step metadata changed");
            }
            recordedSteps.emplace(*row[0],
                                  std::array<std::string, 4>{*row[1], *row[2],
                                                              *row[3], *row[4]});
        }
        if (!versionWasComplete &&
            !updateVersionStatus(executor_, definition, "RUNNING", "")) {
            return failed(std::move(report), "",
                          "migration version running state could not be recorded");
        }
        for (const auto& step : definition.steps) {
            const auto existing = recordedSteps.find(step.id);
            if (step.phase == MigrationPhase::Cleanup) continue;
            if (existing != recordedSteps.end()) {
                const auto& status = existing->second[3];
                if (versionWasComplete && status != "SUCCEEDED") {
                    return failed(std::move(report), step.id,
                                  "completed migration version has an incomplete step record");
                }
                if (status == "SUCCEEDED") {
                    report.alreadyCompleteSteps.push_back(step.id);
                    removePending(report, step.id);
                    continue;
                }
                std::string recoveredError;
                if (verifyStep(executor_, step, recoveredError)) {
                    if (!updateStepStatus(executor_, definition.version, step,
                                          "SUCCEEDED", "")) {
                        return failed(std::move(report), step.id,
                                      "recovered step could not be recorded");
                    }
                    ++report.stepsApplied;
                    report.appliedSteps.push_back(step.id);
                    removePending(report, step.id);
                    continue;
                }
            } else if (versionWasComplete) {
                return failed(std::move(report), step.id,
                              "completed migration version is missing a required step record");
            }

            if (!updateStepStatus(executor_, definition.version, step, "RUNNING", "")) {
                return failed(std::move(report), step.id,
                              "migration running state could not be recorded");
            }

            std::string stepError;
            bool applied = true;
            if (step.execution == MigrationExecution::TransactionalDml) {
                auto transaction = Transaction::begin(executor_, application::WorkUnitMode::Command);
                if (!transaction) {
                    applied = false;
                    stepError = "migration transaction could not start: " +
                                transaction.error().message;
                } else {
                    auto result = executor_.executeTrusted(step.applySql);
                    if (!result) {
                        applied = false;
                        stepError = "migration DML failed: " + result.error().message;
                        transaction.value().rollback();
                    } else if (!verifyStep(executor_, step, stepError)) {
                        applied = false;
                        transaction.value().rollback();
                    } else {
                        auto committed = transaction.value().commit();
                        if (!committed) {
                            applied = false;
                            stepError = "migration DML commit failed: " + committed.error().message;
                        }
                    }
                }
            } else if (step.execution == MigrationExecution::AtomicDdl) {
                report.nonRollbackableSteps.push_back(step.id);
                auto result = executor_.executeTrusted(step.applySql);
                if (!result) {
                    applied = false;
                    stepError = "migration DDL failed: " + result.error().message;
                } else {
                    applied = verifyStep(executor_, step, stepError);
                }
            } else {
                applied = verifyStep(executor_, step, stepError);
            }

            if (!applied) {
                if (!updateStepStatus(executor_, definition.version, step, "FAILED", stepError)) {
                    executor_.poison();
                    stepError += "; additionally failed to record the step failure state";
                }
                if (!updateVersionStatus(executor_, definition, "FAILED", stepError)) {
                    executor_.poison();
                    stepError += "; additionally failed to record the version failure state";
                }
                return failed(std::move(report), step.id, std::move(stepError));
            }
            if (!updateStepStatus(executor_, definition.version, step, "SUCCEEDED", "")) {
                executor_.poison();
                return failed(std::move(report), step.id,
                              "successful migration step could not be recorded");
            }
            ++report.stepsApplied;
            report.appliedSteps.push_back(step.id);
            removePending(report, step.id);
        }
        if (versionWasComplete) {
            ++report.versionsAlreadyComplete;
        } else {
            if (!updateVersionStatus(executor_, definition, "SUCCEEDED", "")) {
                executor_.poison();
                return failed(std::move(report), "",
                              "completed migration version could not be recorded");
            }
            ++report.versionsApplied;
        }
    }
    if (!lock.release()) {
        return failed(std::move(report), "",
                      "migration lock release failed; connection was discarded");
    }
    report.message = "all required migration steps succeeded";
    return report;
}

}  // namespace warehouse::infrastructure::mysql
