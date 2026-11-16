-- Keep the foundation migration immutable. STAGE needs a PENDING audit outcome.
ALTER TABLE `playerbot_autonomy_enrollment_audit`
    MODIFY `state_after` ENUM('ABSENT', 'PENDING', 'PERSISTENT', 'RETIRED') NOT NULL;

-- CREATE/ALTER below are replayable on MySQL 8, including a partially applied update.
CREATE TABLE IF NOT EXISTS `playerbot_autonomy_runtime_schema` (
    `version` INT UNSIGNED NOT NULL,
    PRIMARY KEY (`version`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Audit revisions live in a separate receipt table so existing history is retained
-- without inventing revision numbers for transitions written by an older adapter.
CREATE TABLE IF NOT EXISTS `playerbot_autonomy_transition_receipt` (
    `request_id` CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    `expected_revision` BIGINT UNSIGNED NOT NULL,
    `resulting_revision` BIGINT UNSIGNED NOT NULL,
    `mutation_applied` BIGINT NOT NULL,
    PRIMARY KEY (`request_id`),
    CONSTRAINT `chk_autonomy_single_mutation` CHECK (`mutation_applied` = 1)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

INSERT IGNORE INTO `playerbot_autonomy_runtime_schema` (`version`) VALUES (1);
