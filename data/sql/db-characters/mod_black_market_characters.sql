-- mod-black-market: characters database. The auctions running now. A bid, the gold it takes
-- from the bidder and the refund mail to the bidder before them are saved in one transaction,
-- which is why this table lives here and not in the world database.
-- Undo with data/sql/uninstall/mod_black_market_uninstall_characters.sql.

CREATE TABLE IF NOT EXISTS `mod_black_market_auction` (
  `id` INT UNSIGNED NOT NULL,
  `template_id` INT UNSIGNED NOT NULL COMMENT 'mod_black_market_template id it came from',
  `item` INT UNSIGNED NOT NULL,
  `quantity` INT UNSIGNED NOT NULL DEFAULT 1,
  `min_bid` INT UNSIGNED NOT NULL COMMENT 'opening bid, copper',
  `current_bid` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'copper, 0 = no bids yet',
  `bidder` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'characters.guid of the top bidder',
  `num_bids` INT UNSIGNED NOT NULL DEFAULT 0,
  `start_time` BIGINT UNSIGNED NOT NULL,
  `end_time` BIGINT UNSIGNED NOT NULL,
  `hot` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `ip_state` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `seller` VARCHAR(64) NOT NULL DEFAULT '',
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-black-market: running auctions';

-- next_id: the next auction id, so an id is never handed out twice, restarts included.
-- last_restock: unix time of the last restock, so a shipment missed during downtime comes in at
-- startup.
CREATE TABLE IF NOT EXISTS `mod_black_market_state` (
  `name` VARCHAR(32) NOT NULL,
  `value` BIGINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`name`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-black-market: counters';
