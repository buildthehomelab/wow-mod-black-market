-- mod-black-market: undo the module's world database changes. Run it by hand on the world
-- database after removing the module. It isn't applied automatically: AzerothCore only runs
-- the module's db-world and db-characters folders.
--
-- Removes Mister Chu with his spawn and texts, and the template table (your own rows in it
-- included). It also forgets that the module's updates were applied, so installing the module
-- again applies them again. Idempotent: safe to run again.

DELETE FROM `creature` WHERE `guid` = 9503300;
DELETE FROM `creature_template_model` WHERE `CreatureID` BETWEEN 9503300 AND 9503399;
DELETE FROM `creature_template` WHERE `entry` BETWEEN 9503300 AND 9503399;
DELETE FROM `npc_text` WHERE `ID` BETWEEN 9503300 AND 9503399;

DROP TABLE IF EXISTS `mod_black_market_template`;

DELETE FROM `updates` WHERE `name` LIKE 'mod\_black\_market\_%';
