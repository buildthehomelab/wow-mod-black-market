-- mod-black-market: world database.
--
-- Mister Chu, Madam Goya's man in Booty Bay: creature entry, spawn guid, npc_text 9503300-9503399.
-- He uses the client's own Pandaren Monk model (display 30414) at grown-up size, so no client
-- patch is needed. No stock rows are changed. Idempotent: safe to run again.
-- Undo with data/sql/uninstall/mod_black_market_uninstall_world.sql.

DELETE FROM `creature_template_model` WHERE `CreatureID` BETWEEN 9503300 AND 9503399;
DELETE FROM `creature_template` WHERE `entry` BETWEEN 9503300 AND 9503399;
INSERT INTO `creature_template` (`entry`, `name`, `subname`, `gossip_menu_id`, `minlevel`, `maxlevel`, `faction`, `npcflag`, `speed_walk`, `speed_run`, `BaseAttackTime`, `RangeAttackTime`, `unit_class`, `unit_flags`, `unit_flags2`, `type`, `flags_extra`, `ScriptName`) VALUES
(9503300, 'Mister Chu', 'Black Market', 0, 80, 80, 35, 1, 1, 1.14286, 2000, 2000, 1, 768, 2048, 7, 2, 'npc_black_market_broker');

-- The companion's display is drawn at 0.4; 2.4 on top of that makes him a little taller than a human.
INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`) VALUES
(9503300, 0, 30414, 2.4, 1);

-- Beside Sly Garrett ("Shady Goods") on the inn level of Booty Bay. Only added when the spawn
-- isn't there yet, so a spot picked in game with .npc move survives this file running again.
INSERT IGNORE INTO `creature` (`guid`, `id`, `map`, `spawnMask`, `phaseMask`, `position_x`, `position_y`, `position_z`, `orientation`, `spawntimesecs`, `wander_distance`, `MovementType`, `Comment`) VALUES
(9503300, 9503300, 0, 1, 1, -14439.37, 407.45, 15.1, 1.92, 300, 0, 0, 'mod-black-market: Mister Chu, beside Sly Garrett in Booty Bay');

DELETE FROM `npc_text` WHERE `ID` BETWEEN 9503300 AND 9503304;
INSERT INTO `npc_text` (`ID`, `text0_0`, `text0_1`, `BroadcastTextID0`, `lang0`, `Probability0`) VALUES
(9503300, 'Psst. Over here, $c.$B$BMadam Goya sends her regards. Her associates have... come across a few things the rest of Azeroth thinks are gone for good. Bids only: no buyouts, and no taking a bid back. Whoever holds the top bid when time runs out gets the goods by mail.$B$BWhat catches your eye?', 'Psst. Over here, $c.$B$BMadam Goya sends her regards. Her associates have... come across a few things the rest of Azeroth thinks are gone for good. Bids only: no buyouts, and no taking a bid back. Whoever holds the top bid when time runs out gets the goods by mail.$B$BWhat catches your eye?', 0, 0, 1),
(9503301, 'Here is how it works.$B$BEvery day a new shipment comes in. Each piece takes bids, nothing else. Your bid has to beat the last one by five percent, and once it is in, it stays in.$B$BIf somebody outbids you, your gold comes back by mail. Whoever holds the top bid when the time runs out gets the goods by mail. A bid in the last few minutes keeps the auction open a little longer, so nobody sneaks in at the end.$B$BI only take bids in person. And keep your voice down.', 'Here is how it works.$B$BEvery day a new shipment comes in. Each piece takes bids, nothing else. Your bid has to beat the last one by five percent, and once it is in, it stays in.$B$BIf somebody outbids you, your gold comes back by mail. Whoever holds the top bid when the time runs out gets the goods by mail. A bid in the last few minutes keeps the auction open a little longer, so nobody sneaks in at the end.$B$BI only take bids in person. And keep your voice down.', 0, 0, 1),
(9503302, 'Nothing today, friend. My suppliers are... delayed.$B$BCome back after the next shipment.', 'Nothing today, friend. My suppliers are... delayed.$B$BCome back after the next shipment.', 0, 0, 1),
(9503303, 'Mister Chu leans in and lowers his voice.$B$BA fine piece. Hard to find. Harder to explain where it came from.', 'Mister Chu leans in and lowers his voice.$B$BA fine piece. Hard to find. Harder to explain where it came from.', 0, 0, 1),
(9503304, 'Mister Chu looks you up and down.$B$BThis one is not for you. Not yet.', 'Mister Chu looks you up and down.$B$BThis one is not for you. Not yet.', 0, 0, 1);

-- What can come up for sale. Each restock picks from the enabled rows by weight, never one that
-- is already up. min_bid is the opening bid in gold. ip_state is the mod-individual-progression
-- state a player needs to bid (0 = everybody; ignored without that module): 8 for anything from
-- The Burning Crusade (its TCG and collector's editions included), 13 for Wrath of the Lich King. seller is only
-- shown in the window, like retail's seller column.
-- Rows 1-999 belong to this file and are rewritten when it runs again; add your own from 1000.
CREATE TABLE IF NOT EXISTS `mod_black_market_template` (
  `id` INT UNSIGNED NOT NULL,
  `item` INT UNSIGNED NOT NULL COMMENT 'item_template entry',
  `quantity` INT UNSIGNED NOT NULL DEFAULT 1,
  `min_bid` INT UNSIGNED NOT NULL COMMENT 'opening bid, gold',
  `weight` INT UNSIGNED NOT NULL DEFAULT 100 COMMENT 'relative chance to be picked at a restock',
  `ip_state` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'mod-individual-progression state needed to bid',
  `seller` VARCHAR(64) NOT NULL DEFAULT 'Madam Goya',
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `comment` VARCHAR(255) NOT NULL DEFAULT '',
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-black-market: what the Black Market can sell';

DELETE FROM `mod_black_market_template` WHERE `id` BETWEEN 1 AND 999;
INSERT INTO `mod_black_market_template` (`id`, `item`, `quantity`, `min_bid`, `weight`, `ip_state`, `seller`, `comment`) VALUES
-- Mounts
(1, 13335, 1, 4000, 100, 0, 'A Very Discreet Collector', 'Deathcharger: Baron Rivendare, Stratholme'),
(2, 19872, 1, 3500, 100, 3, 'Madam Goya', 'Swift Razzashi Raptor: Bloodlord Mandokir, Zul''Gurub'),
(3, 19902, 1, 3500, 100, 3, 'Madam Goya', 'Swift Zulian Tiger: High Priest Thekal, Zul''Gurub'),
(4, 21176, 1, 20000, 40, 4, 'A Very Discreet Collector', 'Black Qiraji Resonating Crystal: Scepter of the Shifting Sands'),
(5, 33224, 1, 6000, 100, 8, 'Landro Longshot', 'Reins of the Spectral Tiger: TCG loot'),
(6, 33225, 1, 9000, 80, 8, 'Landro Longshot', 'Reins of the Swift Spectral Tiger: TCG loot'),
(7, 38576, 1, 6000, 100, 8, 'Landro Longshot', 'Big Battle Bear: TCG loot'),
(8, 46778, 1, 6000, 100, 13, 'Landro Longshot', 'Magic Rooster Egg: TCG loot'),
(9, 37719, 1, 5000, 100, 13, 'Madam Goya', 'Swift Zhevra: Recruit-A-Friend'),
(10, 54068, 1, 6000, 100, 13, 'Landro Longshot', 'Wooly White Rhino: TCG loot'),
(11, 23720, 1, 2000, 100, 8, 'Landro Longshot', 'Riding Turtle: TCG loot'),
(12, 33809, 1, 8000, 80, 12, 'Madam Goya', 'Amani War Bear: Zul''Aman timed run'),
(13, 30480, 1, 5000, 100, 8, 'A Very Discreet Collector', 'Fiery Warhorse''s Reins: Attumen the Huntsman, Karazhan'),
(14, 32768, 1, 4000, 100, 8, 'A Very Discreet Collector', 'Reins of the Raven Lord: Anzu, Sethekk Halls heroic'),
(15, 35513, 1, 5000, 100, 12, 'Madam Goya', 'Swift White Hawkstrider: Kael''thas, Magisters'' Terrace heroic'),
(16, 32458, 1, 15000, 40, 9, 'A Very Discreet Collector', 'Ashes of Al''ar: Kael''thas, Tempest Keep'),
(17, 35226, 1, 9000, 80, 8, 'Landro Longshot', 'X-51 Nether-Rocket X-TREME: TCG loot'),
(18, 35225, 1, 6000, 80, 8, 'Landro Longshot', 'X-51 Nether-Rocket: TCG loot'),
(19, 37012, 1, 9000, 80, 8, 'Madam Goya', 'The Horseman''s Reins: Headless Horseman, Hallow''s End'),
(20, 33977, 1, 4000, 100, 8, 'Madam Goya', 'Swift Brewfest Ram: Coren Direbrew, Brewfest'),
(21, 37828, 1, 4000, 100, 8, 'Madam Goya', 'Great Brewfest Kodo: Coren Direbrew, Brewfest'),
(22, 54069, 1, 9000, 80, 8, 'Landro Longshot', 'Blazing Hippogryph: TCG loot'),
(23, 54860, 1, 12000, 60, 13, 'Mei Francis', 'X-53 Touring Rocket: Recruit-A-Friend'),
(24, 50250, 1, 12000, 60, 13, 'Mei Francis', 'Big Love Rocket: Apothecary Hummel, Love is in the Air'),
(25, 54811, 1, 14000, 60, 13, 'Mei Francis', 'Celestial Steed: the store mount'),
(26, 44168, 1, 12000, 60, 13, 'Mei Francis', 'Reins of the Time-Lost Proto-Drake: rare spawn, the Storm Peaks'),
(27, 44151, 1, 8000, 80, 13, 'Mei Francis', 'Reins of the Blue Proto-Drake: Skadi the Ruthless, Utgarde Pinnacle heroic'),
(28, 43986, 1, 9000, 60, 13, 'Mei Francis', 'Reins of the Black Drake: Sartharion with three drakes (10)'),
(29, 43954, 1, 12000, 60, 13, 'Mei Francis', 'Reins of the Twilight Drake: Sartharion with three drakes (25)'),
(30, 43952, 1, 7000, 80, 13, 'Mei Francis', 'Reins of the Azure Drake: Malygos'),
(31, 43953, 1, 7000, 80, 13, 'Mei Francis', 'Reins of the Blue Drake: Malygos'),
(32, 44178, 1, 12000, 60, 13, 'Mei Francis', 'Reins of the Albino Drake: Leading the Cavalry'),
(33, 43962, 1, 6000, 80, 13, 'Mei Francis', 'Reins of the White Polar Bear: Hyldnir Spoils'),
(34, 46109, 1, 4000, 80, 13, 'Mei Francis', 'Sea Turtle: fishing in Northrend'),
(35, 44175, 1, 15000, 40, 13, 'Mei Francis', 'Reins of the Plagued Proto-Drake: Glory of the Raider (10)'),
(36, 45801, 1, 18000, 40, 14, 'Mei Francis', 'Reins of the Ironbound Proto-Drake: Glory of the Ulduar Raider (25)'),
(37, 45693, 1, 25000, 30, 14, 'A Very Discreet Collector', 'Mimiron''s Head: Yogg-Saron without keepers (25)'),
(38, 49636, 1, 18000, 30, 15, 'A Very Discreet Collector', 'Reins of the Onyxian Drake: Onyxia (25)'),
(39, 50818, 1, 30000, 30, 16, 'A Very Discreet Collector', 'Invincible''s Reins: the Lich King heroic (25)'),
-- Companions
(100, 34493, 1, 1500, 60, 8, 'Landro Longshot', 'Dragon Kite: TCG loot'),
(101, 49343, 1, 1000, 60, 13, 'Landro Longshot', 'Spectral Tiger Cub: TCG loot'),
(102, 49287, 1, 1000, 60, 13, 'Landro Longshot', 'Tuskarr Kite: TCG loot'),
(103, 35227, 1, 1000, 60, 8, 'Landro Longshot', 'Goblin Weather Machine: TCG loot'),
(104, 34492, 1, 800, 60, 8, 'Landro Longshot', 'Rocket Chicken: TCG loot'),
(105, 32588, 1, 800, 60, 8, 'Landro Longshot', 'Banana Charm: TCG loot'),
(106, 38050, 1, 800, 60, 8, 'Landro Longshot', 'Soul-Trader Beacon: TCG loot'),
(107, 49646, 1, 750, 60, 13, 'Breanni', 'Core Hound Pup: authenticator'),
(108, 49665, 1, 750, 60, 13, 'Breanni', 'Pandaren Monk: the store pet (a cousin, says Mister Chu)'),
(109, 49693, 1, 750, 60, 13, 'Breanni', 'Lil'' Phylactery: the store pet'),
(110, 54847, 1, 750, 60, 13, 'Breanni', 'Lil'' XT: the store pet'),
(111, 56806, 1, 750, 60, 13, 'Breanni', 'Mini Thor: StarCraft II collector''s edition'),
(112, 13582, 1, 500, 60, 0, 'Breanni', 'Zergling Leash: collector''s edition'),
(113, 13583, 1, 500, 60, 0, 'Breanni', 'Panda Collar: collector''s edition'),
(114, 13584, 1, 500, 60, 0, 'Breanni', 'Diablo Stone: collector''s edition'),
(115, 20371, 1, 500, 60, 0, 'Breanni', 'Blue Murloc Egg: BlizzCon'),
(116, 22114, 1, 500, 60, 0, 'Breanni', 'Pink Murloc Egg: Worldwide Invitational'),
(117, 25535, 1, 500, 60, 8, 'Breanni', 'Netherwhelp''s Collar: collector''s edition'),
(118, 39286, 1, 500, 60, 13, 'Breanni', 'Frosty''s Collar: collector''s edition'),
(119, 39656, 1, 750, 60, 13, 'Breanni', 'Tyrael''s Hilt: BlizzCon'),
(120, 38658, 1, 400, 60, 8, 'Breanni', 'Vampiric Batling: Prince Tenris Mirkblood, Karazhan'),
(121, 35504, 1, 400, 60, 12, 'Breanni', 'Phoenix Hatchling: Kael''thas, Magisters'' Terrace'),
(122, 33993, 1, 400, 60, 12, 'Breanni', 'Mojo: Zul''Aman'),
(123, 41133, 1, 500, 60, 13, 'Breanni', 'Unhatched Mr. Chilly: Battle.net account merger'),
(124, 46802, 1, 500, 60, 13, 'Breanni', 'Heavy Murloc Egg: Worldwide Invitational'),
(125, 54810, 1, 1500, 40, 13, 'Breanni', 'Celestial Dragon: Littlest Pet Shop'),
-- Vanity
(200, 38578, 1, 1000, 40, 8, 'Landro Longshot', 'The Flag of Ownership: TCG loot'),
(201, 38301, 1, 1000, 40, 8, 'Landro Longshot', 'D.I.S.C.O.: TCG loot'),
(202, 33223, 1, 600, 40, 8, 'Landro Longshot', 'Fishing Chair: TCG loot'),
(203, 32566, 1, 600, 40, 8, 'Landro Longshot', 'Picnic Basket: TCG loot'),
(204, 34499, 1, 600, 40, 8, 'Landro Longshot', 'Paper Flying Machine Kit: TCG loot'),
(205, 49703, 1, 1500, 40, 13, 'Landro Longshot', 'Perpetual Purple Firework: TCG loot'),
(206, 49704, 1, 1500, 40, 13, 'Landro Longshot', 'Carved Ogre Idol: TCG loot'),
(207, 54452, 1, 1000, 40, 13, 'Landro Longshot', 'Ethereal Portal: TCG loot'),
(208, 45037, 1, 1500, 30, 13, 'Madam Goya', 'Epic Purple Shirt: the Ulduar shirt nobody can make'),
(209, 38310, 1, 1000, 40, 8, 'Landro Longshot', 'Tabard of the Arcane: TCG loot'),
(210, 38312, 1, 1000, 40, 8, 'Landro Longshot', 'Tabard of Brilliance: TCG loot'),
(211, 38313, 1, 1000, 40, 8, 'Landro Longshot', 'Tabard of Fury: TCG loot'),
(212, 38314, 1, 1000, 40, 8, 'Landro Longshot', 'Tabard of the Defender: TCG loot'),
(213, 38309, 1, 1000, 40, 8, 'Landro Longshot', 'Tabard of Nature: TCG loot'),
(214, 38311, 1, 1000, 40, 8, 'Landro Longshot', 'Tabard of the Void: TCG loot'),
(215, 23705, 1, 800, 40, 8, 'Landro Longshot', 'Tabard of Flame: TCG loot'),
(216, 23709, 1, 800, 40, 8, 'Landro Longshot', 'Tabard of Frost: TCG loot'),
(217, 1973, 1, 1500, 40, 0, 'Sly Garrett', 'Orb of Deception: world drop');
