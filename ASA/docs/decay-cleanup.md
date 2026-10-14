# Purging templates for decayed players (MariaDB, optional)

CreatureColors keeps a player's templates forever. If you run a decay plugin (Lethal Decay or similar)
that removes a player's record when they decay, you can have MariaDB delete that player's templates at
the same moment. Nothing in the plugin changes; this is done entirely in the database.

This only applies with `"UseMysql": true`. With SQLite each server has its own local file, and a database
trigger can't reach across to it.

## 1. Find the decay plugin's player table

You need two names from the decay plugin's database:

- the **table** that has one row per player (or per tribe member) and is deleted from when a player decays
- the **column** in it that holds the player's **EOS ID** (32 hex characters, same value as `eos_id` in
  `creaturecolors_templates`)

In HeidiSQL, open the decay database and look for a table with an EOS ID column, or run:

```sql
SHOW CREATE TABLE <decay_db>.<table>;
```

Also check **how** a player is removed:

- **Row is deleted** -> use the `AFTER DELETE` trigger in section 2.
- **Row stays but gets flagged** (e.g. a `decayed` column set to 1) -> use the `AFTER UPDATE` variant in section 3.

## 2. Trigger: row deleted when a player decays

Replace `<decay_db>`, `<players_table>` and `<eos_column>`. The `creaturecolors` database and table names
match the default config; change them if you renamed either.

```sql
DELIMITER $$

CREATE TRIGGER `<decay_db>`.`creaturecolors_purge_on_decay`
AFTER DELETE ON `<decay_db>`.`<players_table>`
FOR EACH ROW
BEGIN
    DELETE FROM `creaturecolors`.`creaturecolors_templates`
    WHERE `eos_id` = OLD.`<eos_column>`;
END$$

DELIMITER ;
```

## 3. Variant: row flagged instead of deleted

```sql
DELIMITER $$

CREATE TRIGGER `<decay_db>`.`creaturecolors_purge_on_decay`
AFTER UPDATE ON `<decay_db>`.`<players_table>`
FOR EACH ROW
BEGIN
    -- adjust the condition to however the decay plugin marks a decayed player
    IF NEW.`<decayed_column>` = 1 AND OLD.`<decayed_column>` <> 1 THEN
        DELETE FROM `creaturecolors`.`creaturecolors_templates`
        WHERE `eos_id` = NEW.`<eos_column>`;
    END IF;
END$$

DELIMITER ;
```

## 4. Permissions

The trigger runs as the user who created it (its DEFINER). That user needs:

```sql
GRANT TRIGGER ON `<decay_db>`.* TO '<user>'@'localhost';
GRANT DELETE  ON `creaturecolors`.* TO '<user>'@'localhost';
```

Creating it as the MariaDB root/admin account covers both.

## 5. Check it

```sql
SHOW TRIGGERS FROM `<decay_db>`;
```

To test without waiting for a real decay: save a template on a throwaway character, delete (or flag) that
character's row in the decay table by hand, and confirm the template row is gone from
`creaturecolors_templates`.

## Things to know

- **The trigger lives on the decay plugin's table.** If that plugin ever drops and recreates its table
  (some do on updates or wipes), the trigger is lost with it. Re-run `SHOW TRIGGERS` after updating the
  decay plugin.
- **It fires on every delete from that table**, including manual deletes and full wipes. A wipe of the decay
  table would clear every matching template too, which is usually what you want on a wipe.
- **To remove it:**
  ```sql
  DROP TRIGGER `<decay_db>`.`creaturecolors_purge_on_decay`;
  ```

## Catch-up cleanup (one-off)

Templates saved before the trigger existed, by players who have since decayed, can be cleared once with:

```sql
DELETE t FROM `creaturecolors`.`creaturecolors_templates` t
LEFT JOIN `<decay_db>`.`<players_table>` p ON p.`<eos_column>` = t.`eos_id`
WHERE p.`<eos_column>` IS NULL;
```

Only run this if the decay table really contains **every** active player. Otherwise it would delete
templates for players the decay plugin simply hasn't recorded yet. Preview first by swapping
`DELETE t` for `SELECT t.*`.
