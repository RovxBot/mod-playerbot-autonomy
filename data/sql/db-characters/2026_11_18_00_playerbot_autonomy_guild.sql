-- Explicit founding authorization and ownership. Existing guilds are never seeded.
CREATE TABLE IF NOT EXISTS playerbot_autonomy_guild (
    slot INT UNSIGNED NOT NULL,
    founder_guid INT UNSIGNED NOT NULL,
    account_id INT UNSIGNED NOT NULL,
    faction TINYINT UNSIGNED NOT NULL,
    stage_revision BIGINT UNSIGNED NOT NULL,
    stage_request CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    name VARCHAR(24) NOT NULL,
    guild_id INT UNSIGNED NULL,
    phase ENUM('PLANNED','CREATING','MANAGED','QUARANTINED') NOT NULL,
    reason VARCHAR(255) NOT NULL,
    created_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    changed_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    PRIMARY KEY (slot),
    UNIQUE KEY uq_autonomy_founder_stage (stage_request),
    UNIQUE KEY uq_autonomy_founder (founder_guid),
    UNIQUE KEY uq_autonomy_guild_name (name),
    UNIQUE KEY uq_autonomy_managed_guild (guild_id),
    CONSTRAINT chk_autonomy_guild_slot CHECK (slot BETWEEN 1 AND 64),
    CONSTRAINT chk_autonomy_guild_faction CHECK (faction IN (0,1)),
    CONSTRAINT chk_autonomy_managed_id CHECK (phase <> 'MANAGED' OR (guild_id IS NOT NULL AND guild_id > 0))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
CREATE TABLE IF NOT EXISTS playerbot_autonomy_guild_audit (
    slot INT UNSIGNED NOT NULL,
    phase ENUM('PLANNED','CREATING','MANAGED','QUARANTINED') NOT NULL,
    guild_id INT UNSIGNED NULL,
    reason VARCHAR(255) NOT NULL,
    mutation_applied INT NOT NULL,
    created_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    PRIMARY KEY (slot,phase),
    CONSTRAINT chk_autonomy_guild_mutation CHECK (mutation_applied = 1)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
