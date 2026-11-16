-- Module-owned character DB tables; discovered by the playerbots AzerothCore
-- UpdateFetcher from modules/mod-playerbot-autonomy/data/sql/db-characters/.
-- This migration adds no character mutations, seeds no existing bots, and
-- does not register enforcement hooks.
CREATE TABLE IF NOT EXISTS `playerbot_autonomy_enrollment` (
    `character_guid` INT UNSIGNED NOT NULL,
    `account_id` INT UNSIGNED NOT NULL,
    `state` ENUM('PENDING', 'PERSISTENT', 'RETIRED') NOT NULL,
    `guild_id_at_enrollment` INT UNSIGNED NULL DEFAULT NULL,
    `revision` BIGINT UNSIGNED NOT NULL DEFAULT 1,
    `stage_started_at` DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    `first_enrolled_at` DATETIME(6) NULL DEFAULT NULL,
    `state_changed_at` DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    PRIMARY KEY (`character_guid`),
    KEY `idx_autonomy_enrollment_state` (`state`, `character_guid`),
    KEY `idx_autonomy_enrollment_account` (`account_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Append-only audit. No FK cascade: deleting a character must not silently
-- delete enrollment history. A later bounded reconciler must mark orphans.
-- Unique request_id provides a durable replay key for transactional writes.
-- Stage BEFORE changing guild membership; a Pending row guards a crash gap.
CREATE TABLE IF NOT EXISTS `playerbot_autonomy_enrollment_audit` (
    `audit_id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    `request_id` CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    `character_guid` INT UNSIGNED NOT NULL,
    `account_id` INT UNSIGNED NOT NULL,
    `event` ENUM('STAGE', 'CONFIRM', 'CANCEL', 'RETIRE', 'ORPHAN_RECONCILE') NOT NULL,
    `state_before` ENUM('ABSENT', 'PENDING', 'PERSISTENT', 'RETIRED') NOT NULL,
    `state_after` ENUM('ABSENT', 'PERSISTENT', 'RETIRED') NOT NULL,
    `guild_id` INT UNSIGNED NULL DEFAULT NULL,
    `actor_kind` ENUM('RECRUITMENT', 'AUTHORIZED_ADMIN', 'SYSTEM_RECONCILER') NOT NULL,
    `actor_guid` INT UNSIGNED NULL DEFAULT NULL,
    `reason` VARCHAR(255) NOT NULL,
    `created_at` DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    PRIMARY KEY (`audit_id`),
    UNIQUE KEY `uq_autonomy_enrollment_request` (`request_id`),
    KEY `idx_autonomy_enrollment_audit_guid` (`character_guid`, `audit_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
