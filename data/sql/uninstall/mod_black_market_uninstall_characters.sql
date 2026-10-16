-- mod-black-market: undo the module's characters database changes. Run it by hand on the
-- characters database after removing the module.
--
-- The gold of a bid that is still running was taken when the bid was placed. Settle or refund
-- those first: `.bmah end <id>` mails the item to the top bidder. To give the gold back
-- instead, look up the bidders with
--     SELECT `id`, `item`, `current_bid`, `bidder` FROM `mod_black_market_auction` WHERE `bidder` <> 0;
-- and send each of them their `current_bid` (copper) before running this file.
--
-- Mails the module already sent (won items, refunds) are ordinary mail and stay. Idempotent.

DROP TABLE IF EXISTS `mod_black_market_auction`;
DROP TABLE IF EXISTS `mod_black_market_state`;

DELETE FROM `updates` WHERE `name` LIKE 'mod\_black\_market\_%';
