-- Module-only simulated operation journal. No core guild/character changes.
CREATE TABLE IF NOT EXISTS playerbot_autonomy_operation (
    operation_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
    revision BIGINT UNSIGNED NOT NULL,
    guild_id INT UNSIGNED NOT NULL,
    leader_guid INT UNSIGNED NOT NULL,
    faction INT UNSIGNED NOT NULL,
    operation_type INT UNSIGNED NOT NULL,
    state VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    created_at BIGINT UNSIGNED NOT NULL,
    changed_at BIGINT UNSIGNED NOT NULL,
    deadline BIGINT UNSIGNED NOT NULL,
    lease_until BIGINT UNSIGNED NOT NULL,
    attempts INT UNSIGNED NOT NULL,
    reason VARCHAR(255) NOT NULL,
    KEY ix_autonomy_operation_state (state,operation_id),
    CONSTRAINT chk_autonomy_operation_state CHECK (state IN
      ('PROPOSED','VALIDATED','RESERVED','ASSEMBLING','ACTIVE','CLOSING','COMPLETED','FAILED','CANCELLED','RECOVERING')),
    CONSTRAINT chk_autonomy_operation_identity CHECK (operation_id > 0 AND faction IN (0,1) AND operation_type = 0),
    CONSTRAINT chk_autonomy_operation_deadline CHECK (created_at < deadline AND deadline < lease_until)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
CREATE TABLE IF NOT EXISTS playerbot_autonomy_operation_member (
    operation_id BIGINT UNSIGNED NOT NULL,
    position INT UNSIGNED NOT NULL,
    character_guid INT UNSIGNED NOT NULL,
    account_id INT UNSIGNED NOT NULL,
    guild_id INT UNSIGNED NOT NULL,
    combat_role INT UNSIGNED NOT NULL,
    PRIMARY KEY (operation_id,position),
    UNIQUE KEY uq_autonomy_operation_member (operation_id,character_guid),
    CONSTRAINT chk_autonomy_operation_member CHECK (position < 25 AND combat_role BETWEEN 1 AND 3)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
CREATE TABLE IF NOT EXISTS playerbot_autonomy_operation_lease (
    character_guid INT UNSIGNED NOT NULL PRIMARY KEY,
    operation_id BIGINT UNSIGNED NOT NULL,
    account_id INT UNSIGNED NOT NULL,
    expires_at BIGINT UNSIGNED NOT NULL,
    KEY ix_autonomy_lease_operation (operation_id),
    KEY ix_autonomy_lease_expiry (expires_at)
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS playerbot_autonomy_operation_journal (
    operation_id BIGINT UNSIGNED NOT NULL,
    revision BIGINT UNSIGNED NOT NULL,
    state VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    reason VARCHAR(255) NOT NULL,
    mutation_applied INT NOT NULL,
    PRIMARY KEY (operation_id,revision),
    CONSTRAINT chk_autonomy_operation_mutation CHECK (mutation_applied = 1)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
