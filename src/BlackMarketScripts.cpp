/*
 * mod-black-market: Mister Chu (the broker), the world hooks and the .bmah GM commands.
 *
 * Talking to Mister Chu opens the BlackMarket addon's window when the player has the addon (it
 * closes the gossip). Without it the same market works as gossip: one option per auction, then
 * a page with the details and a box to type a bid in gold.
 *
 * Released under the MIT License.
 */

#include "BlackMarket.h"
#include "Chat.h"
#include "ChatCommand.h"
#include "Creature.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "ScriptedGossip.h"
#include <cctype>
#include <mutex>
#include <unordered_map>

using namespace Acore::ChatCommands;

namespace BlackMarket
{
    namespace
    {
        enum Senders : uint32
        {
            SENDER_MAIN  = GOSSIP_SENDER_MAIN,
            SENDER_ITEM  = GOSSIP_SENDER_MAIN + 1,
            SENDER_BID   = GOSSIP_SENDER_MAIN + 2,
            SENDER_RULES = GOSSIP_SENDER_MAIN + 3,
            SENDER_CONFIRM = GOSSIP_SENDER_MAIN + 4
        };

        constexpr std::size_t MAX_GOSSIP_LISTINGS = 20;

        // A bid typed into the gossip box waits here until the player confirms it.
        struct PendingBid
        {
            uint32 auctionId = 0;
            uint32 gold = 0;
        };
        std::mutex sPendingLock;
        std::unordered_map<ObjectGuid::LowType, PendingBid> sPending;

        std::string QualityName(uint32 item)
        {
            std::string link = ItemLink(item);
            // Gossip shows colour codes but not item links: keep "|cffxxxxxx[Name]|r".
            std::size_t h = link.find("|H");
            std::size_t end = link.find("|h", h + 2);
            std::size_t close = link.rfind("|h");
            if (h == std::string::npos || end == std::string::npos || close == std::string::npos || close <= end)
                return link;
            return link.substr(0, h) + link.substr(end + 2, close - end - 2) + "|r";
        }

        std::string LockReason(Listing const& l)
        {
            if (l.flags & LISTING_LEVEL_LOCKED)
                return BidResultText(BidResult::LevelLocked);
            if (l.flags & LISTING_ERA_LOCKED)
                return BidResultText(BidResult::EraLocked);
            if (l.flags & LISTING_KNOWN)
                return BidResultText(BidResult::AlreadyKnown);
            return "";
        }

        void ShowMain(Player* player, Creature* creature)
        {
            ClearGossipMenuFor(player);
            std::vector<Listing> listings = GetListings(player);

            std::size_t shown = 0;
            for (Listing const& l : listings)
            {
                if (shown++ >= MAX_GOSSIP_LISTINGS)
                    break;

                std::string text = (l.flags & LISTING_HOT) ? "|cffff2020Hot!|r " : "";
                text += QualityName(l.item);
                if (l.quantity > 1)
                    text += Acore::StringFormat(" x{}", l.quantity);
                text += Acore::StringFormat("\n   {} - {}", FormatMoney(l.currentBid ? l.currentBid : l.minBid), TimeLeftText(l.timeLeft));
                if (l.flags & LISTING_HIGH_BIDDER)
                    text += " - |cff20a020your bid|r";
                AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, text, SENDER_ITEM, GOSSIP_ACTION_INFO_DEF + l.id);
            }

            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "How does this work?", SENDER_RULES, GOSSIP_ACTION_INFO_DEF);
            SendGossipMenuFor(player, listings.empty() ? TEXT_EMPTY : TEXT_GREETING, creature->GetGUID());
        }

        void ShowItem(Player* player, Creature* creature, uint32 auctionId)
        {
            Listing l;
            if (!GetListing(player, auctionId, l))
            {
                ShowMain(player, creature);
                return;
            }

            ClearGossipMenuFor(player);
            uint32 action = GOSSIP_ACTION_INFO_DEF + l.id;
            std::string name = QualityName(l.item);
            if (l.quantity > 1)
                name += Acore::StringFormat(" x{}", l.quantity);
            if (l.flags & LISTING_HOT)
                name = "|cffff2020Hot!|r " + name;

            AddGossipItemFor(player, GOSSIP_ICON_VENDOR, name + "\n   Seller: " + l.seller, SENDER_ITEM, action);
            if (l.numBids)
                AddGossipItemFor(player, GOSSIP_ICON_DOT, Acore::StringFormat("Current bid: {} ({} bid{})", FormatMoney(l.currentBid),
                    l.numBids, l.numBids == 1 ? "" : "s"), SENDER_ITEM, action);
            else
                AddGossipItemFor(player, GOSSIP_ICON_DOT, "Opening bid: " + FormatMoney(l.minBid), SENDER_ITEM, action);
            AddGossipItemFor(player, GOSSIP_ICON_DOT, Acore::StringFormat("Time left: {}", TimeLeftText(l.timeLeft)), SENDER_ITEM, action);

            std::string lock = LockReason(l);
            if (l.flags & LISTING_HIGH_BIDDER)
                AddGossipItemFor(player, GOSSIP_ICON_DOT, "|cff20a020You hold the highest bid.|r", SENDER_ITEM, action);
            else if (!lock.empty())
                AddGossipItemFor(player, GOSSIP_ICON_DOT, "|cffff2020" + lock + "|r", SENDER_ITEM, action);
            else
            {
                uint32 nextGold = (l.nextBid + GOLD - 1) / GOLD;
                AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, Acore::StringFormat("Place a bid (at least {})", FormatMoney(nextGold * GOLD)),
                    SENDER_BID, action,
                    Acore::StringFormat("Bid on {}.\n\nThe least I take is {} gold. Type your bid in whole gold.", ItemName(l.item), nextGold),
                    0, true);
            }

            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Back", SENDER_MAIN, GOSSIP_ACTION_INFO_DEF);
            SendGossipMenuFor(player, (l.flags & (LISTING_LEVEL_LOCKED | LISTING_ERA_LOCKED)) ? TEXT_LOCKED : TEXT_ITEM, creature->GetGUID());
        }

        // "12,500", "12500g", " 12 500 " -> 12500. False on anything else, a decimal point
        // included: "100.50" must not become 10,050 gold.
        bool ParseGold(char const* code, uint32& gold)
        {
            if (!code)
                return false;
            uint64 value = 0;
            bool digits = false;
            for (char const* c = code; *c; ++c)
            {
                if (std::isdigit(static_cast<unsigned char>(*c)))
                {
                    value = value * 10 + uint64(*c - '0');
                    digits = true;
                    if (value > MAX_MONEY_AMOUNT / GOLD)
                        return false;
                }
                else if (*c != ',' && *c != ' ' && *c != 'g' && *c != 'G')
                    return false;
            }
            if (!digits)
                return false;
            gold = uint32(value);
            return true;
        }

        void ShowConfirm(Player* player, Creature* creature, uint32 auctionId, uint32 gold)
        {
            Listing l;
            if (!GetListing(player, auctionId, l))
            {
                ShowMain(player, creature);
                return;
            }

            ClearGossipMenuFor(player);
            uint32 action = GOSSIP_ACTION_INFO_DEF + auctionId;
            AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, Acore::StringFormat("Yes: bid {} on {}", FormatMoney(gold * GOLD),
                QualityName(l.item)), SENDER_CONFIRM, action);
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "No, let me think about it.", SENDER_ITEM, action);
            SendGossipMenuFor(player, TEXT_ITEM, creature->GetGUID());
        }
    }

    class npc_black_market_broker : public CreatureScript
    {
    public:
        npc_black_market_broker() : CreatureScript("npc_black_market_broker") { }

        bool OnGossipHello(Player* player, Creature* creature) override
        {
            SetBroker(player, creature->GetGUID());

            if (!GetConfig().enabled)
            {
                ClearGossipMenuFor(player);
                SendGossipMenuFor(player, TEXT_EMPTY, creature->GetGUID());
                return true;
            }

            // The gossip goes out either way: the addon closes it when it opens its window, and
            // a player who switched the addon off since logging in still gets the market.
            ShowMain(player, creature);
            if (Addon::HasAddon(player))
                Addon::Open(player);
            return true;
        }

        bool OnGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action) override
        {
            switch (sender)
            {
                case SENDER_ITEM:
                    ShowItem(player, creature, action - GOSSIP_ACTION_INFO_DEF);
                    break;
                case SENDER_CONFIRM:
                {
                    uint32 auctionId = action - GOSSIP_ACTION_INFO_DEF;
                    PendingBid pending;
                    {
                        std::lock_guard<std::mutex> guard(sPendingLock);
                        auto itr = sPending.find(player->GetGUID().GetCounter());
                        if (itr != sPending.end())
                        {
                            pending = itr->second;
                            sPending.erase(itr);
                        }
                    }
                    if (pending.auctionId == auctionId && pending.gold)
                    {
                        BidResult result = PlaceBid(player, auctionId, pending.gold * GOLD);
                        creature->Whisper(BidResultText(result), LANG_UNIVERSAL, player);
                    }
                    ShowItem(player, creature, auctionId);
                    break;
                }
                case SENDER_RULES:
                    ClearGossipMenuFor(player);
                    AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Back", SENDER_MAIN, GOSSIP_ACTION_INFO_DEF);
                    SendGossipMenuFor(player, TEXT_RULES, creature->GetGUID());
                    break;
                default:
                    ShowMain(player, creature);
                    break;
            }
            return true;
        }

        bool OnGossipSelectCode(Player* player, Creature* creature, uint32 sender, uint32 action, char const* code) override
        {
            uint32 auctionId = action - GOSSIP_ACTION_INFO_DEF;
            if (sender != SENDER_BID)
            {
                ShowMain(player, creature);
                return true;
            }

            uint32 gold = 0;
            if (!ParseGold(code, gold) || !gold)
            {
                creature->Whisper("Whole gold, friend. Just the number.", LANG_UNIVERSAL, player);
                ShowItem(player, creature, auctionId);
                return true;
            }

            {
                std::lock_guard<std::mutex> guard(sPendingLock);
                sPending[player->GetGUID().GetCounter()] = { auctionId, gold };
            }
            ShowConfirm(player, creature, auctionId, gold);
            return true;
        }
    };

    class BlackMarketWorldScript : public WorldScript
    {
    public:
        BlackMarketWorldScript() : WorldScript("BlackMarketWorldScript",
            {
                WORLDHOOK_ON_AFTER_CONFIG_LOAD,
                WORLDHOOK_ON_STARTUP,
                WORLDHOOK_ON_UPDATE
            }) { }

        void OnAfterConfigLoad(bool /*reload*/) override
        {
            LoadConfig();
        }

        void OnStartup() override
        {
            Load();
        }

        void OnUpdate(uint32 diff) override
        {
            Update(diff);
        }
    };

    class BlackMarketCommandScript : public CommandScript
    {
    public:
        BlackMarketCommandScript() : CommandScript("BlackMarketCommandScript") { }

        ChatCommandTable GetCommands() const override
        {
            static ChatCommandTable subCommands =
            {
                { "list",    HandleListCommand,    SEC_GAMEMASTER,    Console::Yes },
                { "restock", HandleRestockCommand, SEC_ADMINISTRATOR, Console::Yes },
                { "reload",  HandleReloadCommand,  SEC_ADMINISTRATOR, Console::Yes },
                { "end",     HandleEndCommand,     SEC_ADMINISTRATOR, Console::Yes },
            };

            static ChatCommandTable commandTable =
            {
                { "bmah", subCommands },
            };

            return commandTable;
        }

        static bool HandleListCommand(ChatHandler* handler)
        {
            std::vector<Listing> listings = GetListings(nullptr);
            if (listings.empty())
            {
                handler->SendSysMessage("Black Market: nothing for sale.");
                return true;
            }
            for (Listing const& l : listings)
                handler->SendSysMessage(Acore::StringFormat("#{} {}{} x{} - opening {}, now {} ({} bids) - {} - era {} - {}",
                    l.id, (l.flags & LISTING_HOT) ? "[hot] " : "", ItemLink(l.item), l.quantity, FormatMoney(l.minBid),
                    FormatMoney(l.currentBid), l.numBids, TimeLeftText(l.timeLeft), l.ipState, l.seller));
            return true;
        }

        static bool HandleRestockCommand(ChatHandler* handler)
        {
            uint32 added = Restock(true);
            handler->SendSysMessage(Acore::StringFormat("Black Market: {} new auctions.", added));
            return true;
        }

        static bool HandleReloadCommand(ChatHandler* handler)
        {
            ReloadTemplates();
            handler->SendSysMessage("Black Market: templates reloaded. Running auctions keep their terms.");
            return true;
        }

        static bool HandleEndCommand(ChatHandler* handler, uint32 auctionId)
        {
            if (!ForceEnd(auctionId))
            {
                handler->SendSysMessage(Acore::StringFormat("Black Market: no auction #{}.", auctionId));
                return true;
            }
            handler->SendSysMessage(Acore::StringFormat("Black Market: auction #{} ended and settled.", auctionId));
            return true;
        }
    };
}

void AddBlackMarketScripts()
{
    new BlackMarket::npc_black_market_broker();
    new BlackMarket::BlackMarketWorldScript();
    new BlackMarket::BlackMarketCommandScript();
}
