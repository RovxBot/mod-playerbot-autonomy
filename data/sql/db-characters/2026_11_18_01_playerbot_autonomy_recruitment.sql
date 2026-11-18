-- No existing bots are enrolled or guilds adopted by this migration.
CREATE TABLE IF NOT EXISTS playerbot_autonomy_recruitment (
    character_guid INT UNSIGNED NOT NULL,
    account_id INT UNSIGNED NOT NULL,
    guild_id INT UNSIGNED NOT NULL,
    slot INT UNSIGNED NOT NULL,
    actor_guid INT UNSIGNED NOT NULL,
    stage_revision BIGINT UNSIGNED NOT NULL,
    stage_request CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    phase ENUM('SELECTED','JOINING','COMPLETE','QUARANTINED') NOT NULL,
    reason VARCHAR(255) NOT NULL,
    created_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    changed_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    PRIMARY KEY (character_guid),
    UNIQUE KEY uq_autonomy_recruit_stage (stage_request),
    KEY idx_autonomy_recruit_guild (guild_id,phase)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
CREATE TABLE IF NOT EXISTS playerbot_autonomy_recruitment_audit (
    character_guid INT UNSIGNED NOT NULL,
    phase ENUM('SELECTED','JOINING','COMPLETE','QUARANTINED') NOT NULL,
    guild_id INT UNSIGNED NOT NULL,
    actor_guid INT UNSIGNED NOT NULL,
    reason VARCHAR(255) NOT NULL,
    mutation_applied INT NOT NULL,
    created_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    PRIMARY KEY (character_guid,phase),
    CONSTRAINT chk_autonomy_recruit_mutation CHECK (mutation_applied=1)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
