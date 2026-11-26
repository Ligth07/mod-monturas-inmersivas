CREATE TABLE IF NOT EXISTS `immersive_mount_happiness` (
    `guid` INT UNSIGNED NOT NULL,
    `spell` INT UNSIGNED NOT NULL,
    `happiness` TINYINT UNSIGNED NOT NULL,
    PRIMARY KEY (`guid`, `spell`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
