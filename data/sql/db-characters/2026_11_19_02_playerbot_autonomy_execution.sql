-- Additive M3A ownership receipts. Backfill existing M2 rows exactly once.
-- Keep this table on rollback; never downgrade while a real operation is open.
CREATE TABLE IF NOT EXISTS playerbot_autonomy_execution (
    operation_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
    schema_version INT UNSIGNED NOT NULL DEFAULT 1,
    revision BIGINT UNSIGNED NOT NULL,
    execution_mode INT UNSIGNED NOT NULL,
    group_id INT UNSIGNED NOT NULL,
    creation_fenced INT UNSIGNED NOT NULL,
    succeeded INT UNSIGNED NOT NULL,
    group_confirmed INT UNSIGNED NOT NULL DEFAULT 0,
    CONSTRAINT chk_autonomy_execution_version CHECK (schema_version = 1),
    CONSTRAINT chk_autonomy_execution_mode CHECK (execution_mode IN (0,1)),
    CONSTRAINT chk_autonomy_execution_fence CHECK (creation_fenced IN (0,1) AND succeeded IN (0,1)),
    CONSTRAINT chk_autonomy_execution_confirmed CHECK (group_confirmed IN (0,1) AND (group_confirmed=0 OR group_id>0)),
    CONSTRAINT chk_autonomy_execution_group CHECK (group_id = 0 OR (execution_mode = 1 AND creation_fenced = 1))
) ENGINE=InnoDB;

-- ID zero is a module schema receipt, never an operation. Its presence prevents
-- a later replay from inventing simulated evidence for a missing real receipt.
INSERT IGNORE INTO playerbot_autonomy_execution
 (operation_id,revision,execution_mode,group_id,creation_fenced,succeeded)
SELECT operation_id,revision,0,0,0,0 FROM playerbot_autonomy_operation
WHERE NOT EXISTS (SELECT 1 FROM playerbot_autonomy_execution WHERE operation_id=0);
INSERT IGNORE INTO playerbot_autonomy_execution
 (operation_id,revision,execution_mode,group_id,creation_fenced,succeeded) VALUES (0,0,0,0,0,0);
