# mod-black-market

A Mists of Pandaria style **Black Market Auction House** for AzerothCore (3.3.5a), with a gold
sink's worth of rare mounts, companions and vanity items.

Mister Chu, Madam Goya's man in Booty Bay, is a pandaren in black robes who stands beside Sly
Garrett ("Shady Goods") on the inn level. He uses the client's own Pandaren Monk model at full
size, so no client patch is needed.

## How it works

- Once a day, at `BlackMarket.RefreshHour`, a new shipment comes in: the market fills up to
  `BlackMarket.MaxAuctions` auctions, picked by weight from `mod_black_market_template`. One of
  the priciest new arrivals is the **hot item**.
- Bids only. No buyouts, and a bid can't be taken back. The next bid has to beat the current
  one by 5% (rounded down to whole gold, at least 1 gold).
- The gold is taken when you bid. If somebody outbids you, it comes back by mail.
- Whoever holds the top bid when time runs out gets the item by mail (it keeps for
  `BlackMarket.MailExpiryDays`, 90 by default). Time left shows only as
  Short (under 30 minutes), Medium (under 2 hours), Long (under 12 hours) or Very Long.
- A bid in the last `BlackMarket.SnipeMinutes` minutes keeps the auction open that long.
- Bids are taken in person: you have to be standing with Mister Chu.
- You need `BlackMarket.MinLevel` to bid. With mod-individual-progression, an item also needs
  the progression state in its template row (`ip_state`): 8 for anything from The Burning
  Crusade (its TCG loot and collector's editions too), 13 for Wrath of the Lich King, the
  unlocking raid for raid drops. Everybody can still look.
- A mount or companion you already know can't be bid on (`BlackMarket.RefuseKnown`).
- Auctions survive restarts. One that ran out while the server was down pays out at startup, and
  a shipment missed while it was down comes in then too.

## The window

The `BlackMarket` addon (in `addon/BlackMarket`) is the retail window: today's goods with
Level, Type, Time Left, Seller and Current Bid, the hot item on the right, and a bid box at
the bottom. Talking to Mister Chu opens it. Shift-click links an item, Ctrl-click tries it on.
`/bmah reset` puts the window back in the middle.

Without the addon, Mister Chu offers the same market as gossip: one option per auction, then a
page with the details and a box to type a bid in whole gold, and a last step to confirm it.

The addon uses Blizzard's own frames; DragonUI, when installed, reskins them.

## What's for sale

`data/sql/db-world/mod_black_market_world.sql` ships 83 rows: 39 mounts (TCG and store
mounts, Deathcharger, the Zul'Gurub mounts, the Black Qiraji crystal, Ashes of Al'ar, the
Northrend drakes, Mimiron's Head, Invincible...), 26 companions and 18 vanity items, with
opening bids from 400 to 30,000 gold. By weight, a shipment of 10 is usually about six
mounts, three companions and one vanity item.

Rows 1-999 belong to the module and are rewritten when the file changes. Add your own from id
1000, or set `enabled = 0` on a shipped row to keep it out:

```sql
INSERT INTO mod_black_market_template (id, item, quantity, min_bid, weight, ip_state, seller, comment)
VALUES (1000, 19019, 1, 50000, 10, 3, 'A Very Discreet Collector', 'Thunderfury');
```

Then `.bmah reload`. Running auctions keep their terms.

## Install

```bash
cd azerothcore/modules
git clone https://github.com/buildthehomelab/wow-mod-black-market.git mod-black-market
```

Clone it as `mod-black-market`: AzerothCore names the loader after the folder. Rebuild, copy
`conf/mod_black_market.conf.dist` to your config folder, and start the server. The SQL in
`data/sql` is applied automatically. The first start restocks right away.

Mister Chu may need a nudge to stand where you like: `.npc move` him in game. The spawn is only
inserted when it isn't there, so the spot you pick survives module updates.

Copy `addon/BlackMarket` into the client's `Interface/AddOns`. Realms that hand out addons with
Portalkeeper and mod-realm-config can run `sql/portalkeeper_addon.sql` instead.

## GM commands

| Command | |
|---|---|
| `.bmah list` | the running auctions, bids and terms |
| `.bmah restock` | fill the market up to `MaxAuctions` now |
| `.bmah reload` | reread `mod_black_market_template` |
| `.bmah end <id>` | end one auction now and pay it out |

## Uninstall

Remove the module, rebuild, then run the files in `data/sql/uninstall` by hand. Settle or refund
running bids first: their gold was taken when they were placed (the characters file says how).

## IDs

creature entry, spawn guid and npc_text 9503300-9503399 (uses 9503300-9503304). World table
`mod_black_market_template`, characters tables `mod_black_market_auction` and
`mod_black_market_state`.

## License

MIT.
