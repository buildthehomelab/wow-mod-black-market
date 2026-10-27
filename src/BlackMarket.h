/*
 * mod-black-market: a Mists of Pandaria style Black Market Auction House.
 *
 * A short list of rare goods (mounts, pets, vanity items) goes up for auction once a day. Each
 * auction takes bids only: no buyout, bids can't be taken back, the next bid must beat the last
 * by 5%, and an outbid player gets their gold back by mail. When the time runs out the highest
 * bidder gets the item by mail. Bids are placed in person with the broker (Mister Chu); the
 * BlackMarket addon shows the retail window, players without it get the same market as gossip.
 *
 * Released under the MIT License.
 */

#ifndef MOD_BLACK_MARKET_H
#define MOD_BLACK_MARKET_H

#include "Define.h"
#include "ObjectGuid.h"
#include <ctime>
#include <string>
#include <vector>

class Player;
class Creature;

namespace BlackMarket
{
    constexpr uint32 NPC_ENTRY = 9503300;  // Mister Chu
    constexpr uint32 TEXT_GREETING = 9503300;
    constexpr uint32 TEXT_RULES = 9503301;
    constexpr uint32 TEXT_EMPTY = 9503302;
    constexpr uint32 TEXT_ITEM = 9503303;
    constexpr uint32 TEXT_LOCKED = 9503304;

    constexpr uint32 IP_PROGRESSION_QUEST_BASE = 66000;  // mod-individual-progression: rewarded quest 66000 + state
    constexpr uint8 IP_STATE_MAX = 18;

    struct Config
    {
        bool enabled = true;
        uint32 minLevel = 60;
        uint32 maxAuctions = 10;
        uint32 refreshHour = 18;
        uint32 durationMinHours = 20;
        uint32 durationMaxHours = 30;
        uint32 snipeMinutes = 10;
        uint32 incrementPct = 5;
        bool progressionGate = true;
        bool refuseKnown = true;
        bool announce = true;
        uint32 mailExpiryDays = 90;
    };

    // How long is left, the way the retail window shows it.
    enum TimeLeft : uint8
    {
        TIME_LEFT_ENDED     = 0,
        TIME_LEFT_SHORT     = 1,  // under 30 minutes
        TIME_LEFT_MEDIUM    = 2,  // 30 minutes to 2 hours
        TIME_LEFT_LONG      = 3,  // 2 to 12 hours
        TIME_LEFT_VERY_LONG = 4   // over 12 hours
    };

    // Flags on a listing as one player sees it (the addon reads these).
    enum ListingFlags : uint32
    {
        LISTING_HIGH_BIDDER  = 0x01,  // the viewer holds the top bid
        LISTING_HOT          = 0x02,  // today's hot item
        LISTING_ERA_LOCKED   = 0x04,  // the viewer's progression isn't there yet
        LISTING_LEVEL_LOCKED = 0x08,  // the viewer is below BlackMarket.MinLevel
        LISTING_KNOWN        = 0x10   // the viewer knows the mount or pet, and RefuseKnown is on
    };

    enum class BidResult : uint8
    {
        Ok,
        Disabled,
        NotFound,
        Ended,
        TooLow,
        AlreadyHighBidder,
        NotEnoughMoney,
        LevelLocked,
        EraLocked,
        AlreadyKnown,
        TooFar,
        TooHigh,
        SelfFound
    };

    struct Listing
    {
        uint32 id = 0;
        uint32 item = 0;
        uint32 quantity = 1;
        uint32 nextBid = 0;      // the least a bid may be, copper
        uint32 currentBid = 0;   // copper; 0 = no bids yet
        uint32 minBid = 0;       // the opening bid, copper
        uint32 numBids = 0;
        uint8 timeLeft = TIME_LEFT_ENDED;
        uint8 ipState = 0;
        uint32 flags = 0;
        std::string seller;
    };

    Config const& GetConfig();
    void LoadConfig();

    // Startup: templates from the world DB, live auctions from the characters DB. Auctions that
    // ran out while the server was down are settled here.
    void Load();
    void ReloadTemplates();

    // Ticks from WorldScript::OnUpdate: ends auctions, and restocks at BlackMarket.RefreshHour.
    void Update(uint32 diff);

    // Puts up new auctions until there are BlackMarket.MaxAuctions. Returns how many.
    uint32 Restock(bool announce);

    // Ends one auction now (GM command). False when there's no such auction.
    bool ForceEnd(uint32 auctionId);

    std::vector<Listing> GetListings(Player* viewer);
    bool GetListing(Player* viewer, uint32 auctionId, Listing& out);
    BidResult PlaceBid(Player* player, uint32 auctionId, uint32 bid);
    std::string BidResultText(BidResult result);

    // The broker the player last opened. Bids need them still in reach.
    void SetBroker(Player* player, ObjectGuid broker);
    void ForgetPlayer(Player* player);

    // Item link / name helpers shared by gossip and chat.
    std::string ItemLink(uint32 item);
    std::string ItemName(uint32 item);
    std::string FormatMoney(uint32 copper);
    char const* TimeLeftText(uint8 timeLeft);

    uint8 PlayerState(Player* player);

    // The addon side (BlackMarketAddon.cpp).
    namespace Addon
    {
        bool HasAddon(Player* player);
        void Open(Player* player);
        void SendList(Player* player);
        void NotifyOutbid(ObjectGuid::LowType playerLow, uint32 auctionId);
        void NotifyChanged();
    }
}

#endif
