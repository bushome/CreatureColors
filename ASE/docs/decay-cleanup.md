# Purging templates for decayed players (MariaDB, optional)

CreatureColors keeps a player's templates forever. If you run a decay plugin (Lethal Decay or similar)
that removes a player's record when they decay, you can have MariaDB delete that player's templates at
the same moment. Nothing in the plugin changes; this is done entirely in the database.

This only applies with `"UseMysql": true`. With SQLite each server has its own local file, and a database
trigger can't reach across to it.

## Lethal Decay (ASA) - ready to use

Lethal Decay keeps one row per player **per server** in `decayasa.lethaldecay_tribes` (columns `serverid`,
`EOS_ID`, `tribeid`, ..., `decayedlastlogin`), plus tribe-level rows where `EOS_ID` is `'0'`. When a player
decays, their row is kept and `decayedlastlogin` is set to **-2**.

CreatureColors templates are cluster-wide, so the trigger only purges a player's templates once **every** row
they have, on every server, is decayed. Decaying on one map while still active on another leaves their
templates alone.

```sql
DELIMITER $$

CREATE TRIGGER `decayasa`.`creaturecolors_purge_on_decay`
AFTER UPDATE ON `decayasa`.`lethaldecay_tribes`
FOR EACH ROW
BEGIN
    IF NEW.`decayedlastlogin` = -2
       AND NOT (OLD.`decayedlastlogin` <=> -2)
       AND NEW.`EOS_ID` NOT IN ('0', '')
       AND NOT EXISTS (SELECT 1 FROM `decayasa`.`lethaldecay_tribes`
                       WHERE `EOS_ID` = NEW.`EOS_ID`
                         AND NOT (`decayedlastlogin` <=> -2)) THEN
        DELETE FROM `creaturecolors`.`creaturecolors_templates`
        WHERE `eos_id` = NEW.`EOS_ID`;
    END IF;
END$$

DELIMITER ;
```

It fires only on the change **to** -2, so re-saving an already decayed row does nothing. This table already
has other triggers; MariaDB runs several `AFTER UPDATE` triggers on the same table side by side, so it
doesn't interfere with them.

**Check before relying on it:** look at a tribe you know has decayed and confirm its **player** rows (real
EOS IDs) show -2, not just the tribe row with `EOS_ID` = `'0'`. If only the `'0'` row gets -2, tell me and the
trigger needs to work from the tribe ID instead.

If Lethal Decay also deletes rows later (for example on cleanup), add this companion trigger so a deleted
last row purges too:

```sql
DELIMITER $$

CREATE TRIGGER `decayasa`.`creaturecolors_purge_on_delete`
AFTER DELETE ON `decayasa`.`lethaldecay_tribes`
FOR EACH ROW
BEGIN
    IF OLD.`EOS_ID` NOT IN ('0', '')
       AND NOT EXISTS (SELECT 1 FROM `decayasa`.`lethaldecay_tribes`
                       WHERE `EOS_ID` = OLD.`EOS_ID`
                         AND NOT (`decayedlastlogin` <=> -2)) THEN
        DELETE FROM `creaturecolors`.`creaturecolors_templates`
        WHERE `eos_id` = OLD.`EOS_ID`;
    END IF;
END$$

DELIMITER ;
```

Catch-up for players who decayed before the trigger existed: removes templates whose owner has no
non-decayed row left. Preview first by swapping `DELETE t` for `SELECT t.*`.

```sql
DELETE t FROM `creaturecolors`.`creaturecolors_templates` t
WHERE EXISTS (SELECT 1 FROM `decayasa`.`lethaldecay_tribes` p WHERE p.`EOS_ID` = t.`eos_id`)
  AND NOT EXISTS (SELECT 1 FROM `decayasa`.`lethaldecay_tribes` p
                  WHERE p.`EOS_ID` = t.`eos_id` AND NOT (p.`decayedlastlogin` <=> -2));
```

The first `EXISTS` keeps templates of players Lethal Decay has no record of at all, so a gap in its table
can't wipe anyone by accident.

To remove them later:

```sql
DROP TRIGGER `decayasa`.`creaturecolors_purge_on_decay`;
DROP TRIGGER `decayasa`.`creaturecolors_purge_on_delete`;
```

The rest of this file is the generic version for other decay plugins.

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
