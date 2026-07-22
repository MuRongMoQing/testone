SELECT
  (SELECT COUNT(*) = 4
     AND SUM(column_name = 'id' AND data_type = 'int' AND is_nullable = 'NO'
             AND extra LIKE '%auto_increment%') = 1
     AND SUM(column_name = 'username' AND data_type = 'varchar'
             AND character_maximum_length = 64 AND is_nullable = 'NO') = 1
     AND SUM(column_name = 'password' AND data_type = 'varchar'
             AND character_maximum_length = 128 AND is_nullable = 'NO') = 1
     AND SUM(column_name = 'role' AND data_type = 'varchar'
             AND character_maximum_length = 32 AND is_nullable = 'NO'
             AND column_default = 'viewer') = 1
   FROM information_schema.columns
   WHERE table_schema = DATABASE() AND table_name = 'users')
  AND EXISTS (
    SELECT 1 FROM information_schema.statistics
    WHERE table_schema = DATABASE() AND table_name = 'users'
      AND index_name = 'PRIMARY' AND non_unique = 0
      AND seq_in_index = 1 AND column_name = 'id')
  AND NOT EXISTS (
    SELECT 1 FROM information_schema.statistics
    WHERE table_schema = DATABASE() AND table_name = 'users'
      AND index_name = 'PRIMARY' AND seq_in_index > 1)
  AND EXISTS (
    SELECT 1 FROM information_schema.statistics AS username_index
    WHERE username_index.table_schema = DATABASE()
      AND username_index.table_name = 'users'
      AND username_index.non_unique = 0
      AND username_index.index_name <> 'PRIMARY'
      AND username_index.seq_in_index = 1
      AND username_index.column_name = 'username'
      AND NOT EXISTS (
        SELECT 1 FROM information_schema.statistics AS extra_column
        WHERE extra_column.table_schema = username_index.table_schema
          AND extra_column.table_name = username_index.table_name
          AND extra_column.index_name = username_index.index_name
          AND extra_column.seq_in_index > 1))
  AND EXISTS (
    SELECT 1 FROM information_schema.tables
    WHERE table_schema = DATABASE() AND table_name = 'users'
      AND engine = 'InnoDB' AND table_collation LIKE 'utf8mb4%');
