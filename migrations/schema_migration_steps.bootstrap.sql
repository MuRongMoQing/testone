CREATE TABLE IF NOT EXISTS schema_migration_steps (
    version INT UNSIGNED NOT NULL,
    step_id VARCHAR(128) NOT NULL,
    checksum CHAR(64) NOT NULL,
    phase VARCHAR(16) NOT NULL,
    execution VARCHAR(24) NOT NULL,
    status VARCHAR(16) NOT NULL,
    started_at DATETIME(6) NOT NULL,
    finished_at DATETIME(6) NULL,
    error_message TEXT NULL,
    PRIMARY KEY (version, step_id),
    CONSTRAINT fk_schema_migration_steps_version
        FOREIGN KEY (version) REFERENCES schema_migrations(version)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
