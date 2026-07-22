SELECT
  (SELECT COUNT(*) = 7
     AND SUM(column_name = 'id' AND data_type = 'int' AND is_nullable = 'NO'
             AND extra LIKE '%auto_increment%') = 1
     AND SUM(column_name = 'name' AND data_type = 'varchar'
             AND character_maximum_length = 256 AND is_nullable = 'NO') = 1
     AND SUM(column_name = 'location' AND data_type = 'varchar'
             AND character_maximum_length = 256 AND is_nullable = 'NO'
             AND column_default = '默认货架') = 1
     AND SUM(column_name = 'status' AND data_type = 'varchar'
             AND character_maximum_length = 32 AND is_nullable = 'NO'
             AND column_default = 'stored') = 1
     AND SUM(column_name = 'stored_at' AND data_type = 'datetime'
             AND is_nullable = 'NO'
             AND UPPER(column_default) LIKE 'CURRENT_TIMESTAMP%') = 1
     AND SUM(column_name = 'taken_at' AND data_type = 'datetime'
             AND is_nullable = 'YES' AND column_default IS NULL) = 1
     AND SUM(column_name = 'operator' AND data_type = 'varchar'
             AND character_maximum_length = 64 AND is_nullable = 'NO') = 1
   FROM information_schema.columns
   WHERE table_schema = DATABASE() AND table_name = 'goods')
  AND EXISTS (
    SELECT 1 FROM information_schema.statistics
    WHERE table_schema = DATABASE() AND table_name = 'goods'
      AND index_name = 'PRIMARY' AND non_unique = 0
      AND seq_in_index = 1 AND column_name = 'id')
  AND NOT EXISTS (
    SELECT 1 FROM information_schema.statistics
    WHERE table_schema = DATABASE() AND table_name = 'goods'
      AND index_name = 'PRIMARY' AND seq_in_index > 1)
  AND EXISTS (
    SELECT 1 FROM information_schema.tables
    WHERE table_schema = DATABASE() AND table_name = 'goods'
      AND engine = 'InnoDB' AND table_collation LIKE 'utf8mb4%');
