-- Preserve all receipts while allowing a recovered guild to be quarantined
-- again. The phase CAS plus enforced ROW_COUNT still rejects duplicate writes.
-- Dynamic DDL keeps updater replay safe without altering any core-owned table.
SET @autonomy_guild_journal_ddl = IF(
    EXISTS (SELECT 1 FROM information_schema.columns
            WHERE table_schema=DATABASE() AND table_name='playerbot_autonomy_guild_audit' AND column_name='audit_id'),
    'DO 0',
    'ALTER TABLE playerbot_autonomy_guild_audit DROP PRIMARY KEY, ADD audit_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY FIRST, ADD KEY ix_autonomy_guild_phase (slot,phase)'
);
PREPARE autonomy_guild_journal_upgrade FROM @autonomy_guild_journal_ddl;
EXECUTE autonomy_guild_journal_upgrade;
DEALLOCATE PREPARE autonomy_guild_journal_upgrade;
