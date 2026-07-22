SELECT
  (SELECT COUNT(*) = 9
     AND SUM(column_name = 'version' AND data_type = 'int'
             AND column_type = 'int unsigned' AND is_nullable = 'NO') = 1
     AND SUM(column_name = 'step_id' AND data_type = 'varchar'
             AND character_maximum_length = 128 AND is_nullable = 'NO') = 1
     AND SUM(column_name = 'checksum' AND data_type = 'char'
             AND character_maximum_length = 64 AND is_nullable = 'NO') = 1
     AND SUM(column_name = 'phase' AND data_type = 'varchar'
             AND character_maximum_length = 16 AND is_nullable = 'NO') = 1
     AND SUM(column_name = 'execution' AND data_type = 'varchar'
             AND character_maximum_length = 24 AND is_nullable = 'NO') = 1
     AND SUM(column_name = 'status' AND data_type = 'varchar'
             AND character_maximum_length = 16 AND is_nullable = 'NO') = 1
     AND SUM(column_name = 'started_at' AND data_type = 'datetime'
             AND datetime_precision = 6 AND is_nullable = 'NO') = 1
     AND SUM(column_name = 'finished_at' AND data_type = 'datetime'
             AND datetime_precision = 6 AND is_nullable = 'YES') = 1
     AND SUM(column_name = 'error_message' AND data_type = 'text'
             AND is_nullable = 'YES') = 1
   FROM information_schema.columns
   WHERE table_schema = DATABASE() AND table_name = 'schema_migration_steps')
  AND (SELECT COUNT(*) = 2
         AND SUM(column_name = 'version' AND seq_in_index = 1) = 1
         AND SUM(column_name = 'step_id' AND seq_in_index = 2) = 1
       FROM information_schema.statistics
       WHERE table_schema = DATABASE() AND table_name = 'schema_migration_steps'
         AND index_name = 'PRIMARY')
  AND EXISTS (
    SELECT 1 FROM information_schema.key_column_usage
    WHERE table_schema = DATABASE() AND table_name = 'schema_migration_steps'
      AND constraint_name = 'fk_schema_migration_steps_version'
      AND column_name = 'version'
      AND referenced_table_schema = DATABASE()
      AND referenced_table_name = 'schema_migrations'
      AND referenced_column_name = 'version')
  AND EXISTS (
    SELECT 1 FROM information_schema.tables
    WHERE table_schema = DATABASE() AND table_name = 'schema_migration_steps'
      AND engine = 'InnoDB' AND table_collation LIKE 'utf8mb4%');
