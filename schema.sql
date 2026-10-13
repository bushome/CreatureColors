-- CreatureColors plugin
-- Reference only: each plugin runs its own statement with CREATE TABLE IF NOT EXISTS
-- on load, using Mysql.TableName from config.json. The server owner only creates the database.
--
-- Region columns: actual color index captured from the creature.
--   255 = region not used by this species -> skipped on apply.
--   0   = ARK "unset" (mutation-only) -> stored and reapplied as-is.

-- ============ ASE (Ark Server API) — owner keyed by SteamID64 ============
CREATE TABLE IF NOT EXISTS `creaturecolors_templates` (
  `id`              INT UNSIGNED       NOT NULL AUTO_INCREMENT,
  `steam_id`        BIGINT UNSIGNED    NOT NULL,
  `template_name`   VARCHAR(32)        NOT NULL,
  `species_class`   VARCHAR(255)       NOT NULL,  -- blueprint class, used for same-species check
  `region_0`        TINYINT UNSIGNED   NOT NULL,
  `region_1`        TINYINT UNSIGNED   NOT NULL,
  `region_2`        TINYINT UNSIGNED   NOT NULL,
  `region_3`        TINYINT UNSIGNED   NOT NULL,
  `region_4`        TINYINT UNSIGNED   NOT NULL,
  `region_5`        TINYINT UNSIGNED   NOT NULL,
  `created_at`      TIMESTAMP          NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at`      TIMESTAMP          NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uq_owner_name` (`steam_id`, `template_name`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ============ ASA (AsaApi) — owner keyed by EOS ID (32 hex chars) ============
CREATE TABLE IF NOT EXISTS `creaturecolors_templates` (
  `id`              INT UNSIGNED       NOT NULL AUTO_INCREMENT,
  `eos_id`          CHAR(32)           NOT NULL,
  `template_name`   VARCHAR(32)        NOT NULL,
  `species_class`   VARCHAR(255)       NOT NULL,
  `region_0`        TINYINT UNSIGNED   NOT NULL,
  `region_1`        TINYINT UNSIGNED   NOT NULL,
  `region_2`        TINYINT UNSIGNED   NOT NULL,
  `region_3`        TINYINT UNSIGNED   NOT NULL,
  `region_4`        TINYINT UNSIGNED   NOT NULL,
  `region_5`        TINYINT UNSIGNED   NOT NULL,
  `created_at`      TIMESTAMP          NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at`      TIMESTAMP          NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uq_owner_name` (`eos_id`, `template_name`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Save / confirmed overwrite (same row reused via the unique key):
-- INSERT INTO `creaturecolors_templates`
--   (<owner_col>, template_name, species_class, region_0, ..., region_5)
-- VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
-- ON DUPLICATE KEY UPDATE species_class = VALUES(species_class),
--   region_0 = VALUES(region_0), ..., region_5 = VALUES(region_5);