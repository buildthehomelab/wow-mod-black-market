/*
 * mod-black-market: the market itself. Templates (what can come up for sale) live in the world
 * table mod_black_market_template; the auctions running now live in the characters table
 * mod_black_market_auction, so a bid and the gold it takes are saved in one transaction.
 *
 * Released under the MIT License.
 */

#include "BlackMarket.h"
#include "CharacterCache.h"
#include "Chat.h"
#include "Config.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Item.h"
#include "Log.h"
#include "Mail.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Random.h"
#include "Timer.h"
#if __has_include("ChallengesApi.h")
#include "ChallengesApi.h"
#endif
#include <algorithm>
#include <map>
#include <mutex>
#include <unordered_map>

namespace BlackMarket
{
    namespace
    {
        struct Template
        {
            uint32 id = 0;
            uint32 item = 0;
            uint32 quantity = 1;
            uint32 minBidGold = 0;
            uint32 weight = 100;
            uint8 ipState = 0;
            std::string seller;
        };

        struct Auction
        {
            uint32 id = 0;
            uint32 templateId = 0;
            uint32 item = 0;
            uint32 quantity = 1;
            uint32 minBid = 0;      // copper
            uint32 currentBid = 0;  // copper
            uint32 numBids = 0;
            ObjectGuid::LowType bidder = 0;
            time_t startTime = 0;
            time_t endTime = 0;
            bool hot = false;
            uint8 ipState = 0;
            std::string seller;
        };

        Config sConfig;
        std::recursive_mutex sLock;
        std::map<uint32, Template> sTemplates;
        std::map<uint32, Auction> sAuctions;
        std::unordered_map<ObjectGuid::LowType, ObjectGuid> sBrokers;
        uint32 sNextId = 1;
        time_t sLastRestock = 0;
        time_t sNextRestock = 0;
        uint32 sTickTimer = 0;
        bool sLoaded = false;

        time_t Now()
        {
            return GameTime::GetGameTime().count();
        }

        uint32 NextBid(Auction const& a)
        {
            if (!a.numBids)
                return a.minBid;

            uint64 step = uint64(a.currentBid) * sConfig.incrementPct / 100;
            step -= step % GOLD;
            if (step < GOLD)
                step = GOLD;

            return uint32(std::min<uint64>(uint64(a.currentBid) + step, uint64(MAX_MONEY_AMOUNT) + 1));
        }

        uint8 TimeLeftOf(Auction const& a, time_t now)
        {
            if (a.endTime <= now)
                return TIME_LEFT_ENDED;
            time_t left = a.endTime - now;
            if (left < 30 * MINUTE)
                return TIME_LEFT_SHORT;
            if (left < 2 * HOUR)
                return TIME_LEFT_MEDIUM;
            if (left < 12 * HOUR)
                return TIME_LEFT_LONG;
            return TIME_LEFT_VERY_LONG;
        }

        // A mount or companion item teaches a spell (spell 483/55884 + the spell in slot 2).
        uint32 LearnedSpell(ItemTemplate const* proto)
        {
            if (!proto)
                return 0;
            if (proto->Spells[1].SpellTrigger == ITEM_SPELLTRIGGER_LEARN_SPELL_ID && proto->Spells[1].SpellId > 0)
                return uint32(proto->Spells[1].SpellId);
            return 0;
        }

        bool KnowsItem(Player* player, uint32 item)
        {
            uint32 spell = LearnedSpell(sObjectMgr->GetItemTemplate(item));
            return spell && player->HasSpell(spell);
        }

        bool ProgressionInstalled()
        {
            return sObjectMgr->GetQuestTemplate(IP_PROGRESSION_QUEST_BASE + 1) != nullptr;
        }

        std::string Seller(std::string text)
        {
            // The addon splits rows on these.
            for (char& c : text)
                if (c == ',' || c == ';' || c == ':' || c == '|' || c == '\t' || c == '\n')
                    c = ' ';
            return text;
        }

        void SaveAuction(Auction const& a, CharacterDatabaseTransaction trans)
        {
            std::string seller = a.seller;
            CharacterDatabase.EscapeString(seller);
            trans->Append("REPLACE INTO `mod_black_market_auction` (`id`, `template_id`, `item`, `quantity`, `min_bid`, "
                "`current_bid`, `bidder`, `num_bids`, `start_time`, `end_time`, `hot`, `ip_state`, `seller`) "
                "VALUES ({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, '{}')",
                a.id, a.templateId, a.item, a.quantity, a.minBid, a.currentBid, a.bidder, a.numBids,
                uint64(a.startTime), uint64(a.endTime), a.hot ? 1 : 0, a.ipState, seller);
        }

        // The id counter and the last restock survive restarts, so ids are never handed out twice
        // and a shipment missed while the server was down comes in at startup.
        void SaveState(CharacterDatabaseTransaction trans)
        {
            trans->Append("REPLACE INTO `mod_black_market_state` (`name`, `value`) VALUES ('next_id', {}), ('last_restock', {})",
                sNextId, uint64(sLastRestock));
        }

        void Tell(ObjectGuid::LowType low, std::string const& text)
        {
            if (Player* player = ObjectAccessor::FindConnectedPlayer(ObjectGuid::Create<HighGuid::Player>(low)))
                ChatHandler(player->GetSession()).SendSysMessage(text);
        }

        // Gives the previous top bid back by mail.
        void RefundBid(Auction const& a, CharacterDatabaseTransaction trans)
        {
            if (!a.bidder || !a.currentBid)
                return;

            ObjectGuid guid = ObjectGuid::Create<HighGuid::Player>(a.bidder);
            if (!sCharacterCache->GetCharacterAccountIdByGuid(guid))
                return;  // the character is gone

            Player* online = ObjectAccessor::FindConnectedPlayer(guid);
            std::string name = ItemName(a.item);
            MailDraft(Acore::StringFormat("Outbid: {}", name),
                Acore::StringFormat("Somebody wanted the {} more than you did. Here is your {} back, every coin of it.\n\n"
                    "Don't make a scene.\n- Mister Chu", name, FormatMoney(a.currentBid)))
                .AddMoney(a.currentBid)
                .SendMailTo(trans, MailReceiver(online, a.bidder), MailSender(MAIL_CREATURE, NPC_ENTRY), MAIL_CHECK_MASK_COPIED, 0,
                    sConfig.mailExpiryDays);
        }

        // Sends the item to the winner (or nothing, without a bid) and drops the auction.
        void Settle(Auction const& a, CharacterDatabaseTransaction trans)
        {
            trans->Append("DELETE FROM `mod_black_market_auction` WHERE `id` = {}", a.id);

            if (!a.bidder)
            {
                LOG_INFO("module", "mod-black-market: auction {} ({}) ended without a bid", a.id, ItemName(a.item));
                return;
            }

            ObjectGuid guid = ObjectGuid::Create<HighGuid::Player>(a.bidder);
            if (!sCharacterCache->GetCharacterAccountIdByGuid(guid))
            {
                LOG_INFO("module", "mod-black-market: auction {} ({}) ended, winner {} no longer exists", a.id, ItemName(a.item), a.bidder);
                return;
            }

            Player* online = ObjectAccessor::FindConnectedPlayer(guid);
            std::string name = ItemName(a.item);
            Item* item = Item::CreateItem(a.item, a.quantity, nullptr);
            if (!item)
            {
                // The item is gone from item_template: give the gold back instead.
                LOG_ERROR("module", "mod-black-market: auction {} item {} no longer exists, refunding {}", a.id, a.item, a.currentBid);
                RefundBid(a, trans);
                return;
            }

            item->SetOwnerGUID(guid);
            item->SaveToDB(trans);
            MailDraft(Acore::StringFormat("Black Market: {}", name),
                Acore::StringFormat("Pleasure doing business. The {} is yours, for {}.\n\n"
                    "You didn't get it from me.\n- Mister Chu", name, FormatMoney(a.currentBid)))
                .AddItem(item)
                .SendMailTo(trans, MailReceiver(online, a.bidder), MailSender(MAIL_CREATURE, NPC_ENTRY), MAIL_CHECK_MASK_COPIED, 0,
                    sConfig.mailExpiryDays);

            LOG_INFO("module", "mod-black-market: auction {} ({}) won by {} for {} copper after {} bids",
                a.id, name, a.bidder, a.currentBid, a.numBids);

            if (online)
                ChatHandler(online->GetSession()).SendSysMessage(
                    Acore::StringFormat("|cff9d9d9dBlack Market:|r you won {}. Check your mailbox.", ItemLink(a.item)));
        }

        void EndDue()
        {
            time_t now = Now();
            std::vector<uint32> done;
            for (auto const& [id, a] : sAuctions)
                if (a.endTime <= now)
                    done.push_back(id);

            if (done.empty())
                return;

            CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
            for (uint32 id : done)
            {
                Settle(sAuctions[id], trans);
                sAuctions.erase(id);
            }
            CharacterDatabase.CommitTransaction(trans);
            Addon::NotifyChanged();
        }

        void Announce(std::string const& text)
        {
            for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
            {
                if (!player || !player->IsInWorld() || !player->GetSession() || player->GetSession()->IsHeadless()
                    || player->GetLevel() < sConfig.minLevel)
                    continue;
                ChatHandler(player->GetSession()).SendSysMessage(text);
            }
        }

        uint32 RestockLocked(bool announce)
        {
            time_t now = Now();
            sLastRestock = now;

            std::vector<Template const*> pool;
            uint64 totalWeight = 0;
            for (auto const& [id, t] : sTemplates)
            {
                bool listed = std::any_of(sAuctions.begin(), sAuctions.end(),
                    [&](auto const& pair) { return pair.second.templateId == id; });
                if (listed || !t.weight)
                    continue;
                pool.push_back(&t);
                totalWeight += t.weight;
            }

            uint32 want = sAuctions.size() < sConfig.maxAuctions ? sConfig.maxAuctions - uint32(sAuctions.size()) : 0;
            std::vector<Auction*> added;
            CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

            while (want && !pool.empty() && totalWeight)
            {
                // Weighted pick without putting it back.
                uint64 roll = urand(0, uint32(std::min<uint64>(totalWeight - 1, 0xFFFFFFFE)));
                std::size_t pick = 0;
                for (; pick < pool.size(); ++pick)
                {
                    if (roll < pool[pick]->weight)
                        break;
                    roll -= pool[pick]->weight;
                }
                if (pick >= pool.size())
                    pick = pool.size() - 1;

                Template const* t = pool[pick];
                totalWeight -= t->weight;
                pool.erase(pool.begin() + pick);

                uint32 hours = urand(std::min(sConfig.durationMinHours, sConfig.durationMaxHours),
                    std::max(sConfig.durationMinHours, sConfig.durationMaxHours));

                Auction a;
                a.id = sNextId++;
                a.templateId = t->id;
                a.item = t->item;
                a.quantity = t->quantity;
                a.minBid = uint32(std::min<uint64>(uint64(t->minBidGold) * GOLD, MAX_MONEY_AMOUNT));
                a.startTime = now;
                // Minutes too, so the day's auctions don't all end on the hour.
                a.endTime = now + hours * HOUR + urand(0, 59) * MINUTE;
                a.ipState = t->ipState;
                a.seller = t->seller;
                auto [itr, _] = sAuctions.emplace(a.id, a);
                added.push_back(&itr->second);
                --want;
            }

            bool hasHot = std::any_of(sAuctions.begin(), sAuctions.end(), [](auto const& pair) { return pair.second.hot; });
            Auction* hot = nullptr;
            if (!hasHot && !added.empty())
            {
                // One of the three priciest new arrivals.
                std::sort(added.begin(), added.end(), [](Auction* l, Auction* r) { return l->minBid > r->minBid; });
                hot = added[urand(0, uint32(std::min<std::size_t>(added.size(), 3) - 1))];
                hot->hot = true;
            }

            for (Auction* a : added)
                SaveAuction(*a, trans);
            SaveState(trans);
            CharacterDatabase.CommitTransaction(trans);

            if (!added.empty())
            {
                LOG_INFO("module", "mod-black-market: {} new auctions, {} running", added.size(), sAuctions.size());
                if (announce && sConfig.announce)
                {
                    if (hot)
                        Announce(Acore::StringFormat("|cff9d9d9dWhispers in the shadows:|r the Black Market has new goods. Today's hot item: {}.", ItemLink(hot->item)));
                    else
                        Announce("|cff9d9d9dWhispers in the shadows:|r the Black Market has new goods.");
                }
                Addon::NotifyChanged();
            }

            return uint32(added.size());
        }

        Listing MakeListing(Auction const& a, Player* viewer, time_t now, uint8 viewerState)
        {
            Listing l;
            l.id = a.id;
            l.item = a.item;
            l.quantity = a.quantity;
            l.nextBid = NextBid(a);
            l.currentBid = a.currentBid;
            l.minBid = a.minBid;
            l.numBids = a.numBids;
            l.timeLeft = TimeLeftOf(a, now);
            l.ipState = a.ipState;
            l.seller = Seller(a.seller);
            if (a.hot)
                l.flags |= LISTING_HOT;
            if (viewer)
            {
                if (a.bidder && a.bidder == viewer->GetGUID().GetCounter())
                    l.flags |= LISTING_HIGH_BIDDER;
                if (viewer->GetLevel() < sConfig.minLevel)
                    l.flags |= LISTING_LEVEL_LOCKED;
                if (a.ipState > viewerState)
                    l.flags |= LISTING_ERA_LOCKED;
                if (sConfig.refuseKnown && KnowsItem(viewer, a.item))
                    l.flags |= LISTING_KNOWN;
            }
            return l;
        }
    }

    Config const& GetConfig()
    {
        return sConfig;
    }

    void LoadConfig()
    {
        std::lock_guard<std::recursive_mutex> guard(sLock);
        sConfig.enabled = sConfigMgr->GetOption<bool>("BlackMarket.Enable", true);
        sConfig.minLevel = sConfigMgr->GetOption<uint32>("BlackMarket.MinLevel", 60);
        sConfig.maxAuctions = std::clamp<uint32>(sConfigMgr->GetOption<uint32>("BlackMarket.MaxAuctions", 10), 1, 50);
        sConfig.refreshHour = std::min<uint32>(sConfigMgr->GetOption<uint32>("BlackMarket.RefreshHour", 18), 23);
        sConfig.durationMinHours = std::clamp<uint32>(sConfigMgr->GetOption<uint32>("BlackMarket.Duration.MinHours", 20), 1, 24 * 14);
        sConfig.durationMaxHours = std::clamp<uint32>(sConfigMgr->GetOption<uint32>("BlackMarket.Duration.MaxHours", 30), 1, 24 * 14);
        sConfig.snipeMinutes = std::min<uint32>(sConfigMgr->GetOption<uint32>("BlackMarket.SnipeMinutes", 10), 24 * 60);
        sConfig.incrementPct = std::clamp<uint32>(sConfigMgr->GetOption<uint32>("BlackMarket.IncrementPct", 5), 1, 100);
        sConfig.progressionGate = sConfigMgr->GetOption<bool>("BlackMarket.ProgressionGate", true);
        sConfig.refuseKnown = sConfigMgr->GetOption<bool>("BlackMarket.RefuseKnown", true);
        sConfig.announce = sConfigMgr->GetOption<bool>("BlackMarket.Announce", true);
        sConfig.mailExpiryDays = std::clamp<uint32>(sConfigMgr->GetOption<uint32>("BlackMarket.MailExpiryDays", 90), 1, 365);

        if (sLoaded)
            sNextRestock = Acore::Time::GetLocalHourTimestamp(Now(), uint8(sConfig.refreshHour));
    }

    void ReloadTemplates()
    {
        std::lock_guard<std::recursive_mutex> guard(sLock);
        sTemplates.clear();

        QueryResult result = WorldDatabase.Query("SELECT `id`, `item`, `quantity`, `min_bid`, `weight`, `ip_state`, `seller` "
            "FROM `mod_black_market_template` WHERE `enabled` = 1");
        if (!result)
        {
            LOG_WARN("module", "mod-black-market: no templates in mod_black_market_template, nothing will go up for sale");
            return;
        }

        do
        {
            Field* f = result->Fetch();
            Template t;
            t.id = f[0].Get<uint32>();
            t.item = f[1].Get<uint32>();
            t.quantity = std::max<uint32>(f[2].Get<uint32>(), 1);
            t.minBidGold = f[3].Get<uint32>();
            t.weight = f[4].Get<uint32>();
            t.ipState = f[5].Get<uint8>();
            t.seller = f[6].Get<std::string>();

            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(t.item);
            if (!proto)
            {
                LOG_ERROR("module", "mod-black-market: template {} sells item {}, which doesn't exist; skipped", t.id, t.item);
                continue;
            }
            if (t.quantity > std::max<uint32>(proto->GetMaxStackSize(), 1))
                t.quantity = std::max<uint32>(proto->GetMaxStackSize(), 1);

            sTemplates[t.id] = t;
        } while (result->NextRow());

        LOG_INFO("module", "mod-black-market: {} templates", sTemplates.size());
    }

    void Load()
    {
        std::lock_guard<std::recursive_mutex> guard(sLock);
        ReloadTemplates();

        sAuctions.clear();
        QueryResult result = CharacterDatabase.Query("SELECT `id`, `template_id`, `item`, `quantity`, `min_bid`, `current_bid`, "
            "`bidder`, `num_bids`, `start_time`, `end_time`, `hot`, `ip_state`, `seller` FROM `mod_black_market_auction`");
        if (result)
        {
            do
            {
                Field* f = result->Fetch();
                Auction a;
                a.id = f[0].Get<uint32>();
                a.templateId = f[1].Get<uint32>();
                a.item = f[2].Get<uint32>();
                a.quantity = f[3].Get<uint32>();
                a.minBid = f[4].Get<uint32>();
                a.currentBid = f[5].Get<uint32>();
                a.bidder = f[6].Get<uint32>();
                a.numBids = f[7].Get<uint32>();
                a.startTime = time_t(f[8].Get<uint64>());
                a.endTime = time_t(f[9].Get<uint64>());
                a.hot = f[10].Get<uint8>() != 0;
                a.ipState = f[11].Get<uint8>();
                a.seller = f[12].Get<std::string>();
                sAuctions[a.id] = a;
            } while (result->NextRow());
        }

        sNextId = 1;
        sLastRestock = 0;
        if (QueryResult state = CharacterDatabase.Query("SELECT `name`, `value` FROM `mod_black_market_state`"))
        {
            do
            {
                Field* f = state->Fetch();
                std::string name = f[0].Get<std::string>();
                uint64 value = f[1].Get<uint64>();
                if (name == "next_id")
                    sNextId = uint32(std::min<uint64>(value, 0xFFFFFFFF));
                else if (name == "last_restock")
                    sLastRestock = time_t(value);
            } while (state->NextRow());
        }
        if (!sAuctions.empty())
            sNextId = std::max(sNextId, sAuctions.rbegin()->first + 1);
        sNextId = std::max<uint32>(sNextId, 1);

        sLoaded = true;
        LOG_INFO("module", "mod-black-market: {} running auctions", sAuctions.size());

        // Ran out while the server was down.
        EndDue();

        sNextRestock = Acore::Time::GetLocalHourTimestamp(Now(), uint8(sConfig.refreshHour));
        // The shipment of the last restock hour came in only if a restock ran since then. A fresh
        // install, or a server that was down at the restock hour, gets it now.
        if (sConfig.enabled && sLastRestock < sNextRestock - time_t(DAY))
            RestockLocked(false);
    }

    void Update(uint32 diff)
    {
        sTickTimer += diff;
        if (sTickTimer < 5 * IN_MILLISECONDS)
            return;
        sTickTimer = 0;

        std::lock_guard<std::recursive_mutex> guard(sLock);
        if (!sLoaded)
            return;

        EndDue();

        if (sConfig.enabled && Now() >= sNextRestock)
        {
            sNextRestock = Acore::Time::GetLocalHourTimestamp(Now(), uint8(sConfig.refreshHour));
            RestockLocked(true);
        }
    }

    uint32 Restock(bool announce)
    {
        std::lock_guard<std::recursive_mutex> guard(sLock);
        return RestockLocked(announce);
    }

    bool ForceEnd(uint32 auctionId)
    {
        std::lock_guard<std::recursive_mutex> guard(sLock);
        auto itr = sAuctions.find(auctionId);
        if (itr == sAuctions.end())
            return false;
        itr->second.endTime = Now();
        EndDue();
        return true;
    }

    uint8 PlayerState(Player* player)
    {
        if (!sConfig.progressionGate || !ProgressionInstalled())
            return IP_STATE_MAX;
        for (uint8 state = IP_STATE_MAX; state > 0; --state)
            if (player->GetQuestStatus(IP_PROGRESSION_QUEST_BASE + state) == QUEST_STATUS_REWARDED)
                return state;
        return 0;
    }

    std::vector<Listing> GetListings(Player* viewer)
    {
        std::lock_guard<std::recursive_mutex> guard(sLock);
        std::vector<Listing> out;
        time_t now = Now();
        uint8 state = viewer ? PlayerState(viewer) : IP_STATE_MAX;
        for (auto const& [id, a] : sAuctions)
            if (a.endTime > now)
                out.push_back(MakeListing(a, viewer, now, state));

        // Hot item first, then by time left, soonest first.
        std::stable_sort(out.begin(), out.end(), [](Listing const& l, Listing const& r)
        {
            bool lh = l.flags & LISTING_HOT, rh = r.flags & LISTING_HOT;
            if (lh != rh)
                return lh;
            return l.timeLeft < r.timeLeft;
        });
        return out;
    }

    bool GetListing(Player* viewer, uint32 auctionId, Listing& out)
    {
        std::lock_guard<std::recursive_mutex> guard(sLock);
        auto itr = sAuctions.find(auctionId);
        time_t now = Now();
        if (itr == sAuctions.end() || itr->second.endTime <= now)
            return false;
        out = MakeListing(itr->second, viewer, now, viewer ? PlayerState(viewer) : IP_STATE_MAX);
        return true;
    }

    BidResult PlaceBid(Player* player, uint32 auctionId, uint32 bid)
    {
        if (!sConfig.enabled)
            return BidResult::Disabled;

        // In person only, like retail.
        ObjectGuid broker;
        {
            std::lock_guard<std::recursive_mutex> guard(sLock);
            auto itr = sBrokers.find(player->GetGUID().GetCounter());
            if (itr != sBrokers.end())
                broker = itr->second;
        }
        if (!broker || !player->GetNPCIfCanInteractWith(broker, UNIT_NPC_FLAG_GOSSIP))
            return BidResult::TooFar;

#if __has_include("ChallengesApi.h")
        // mod-challenges: the Black Market is an auction house, and Self-Found characters use none.
        if (Challenges::IsSelfFound(player->GetGUID().GetCounter()))
            return BidResult::SelfFound;
#endif

        if (player->GetLevel() < sConfig.minLevel)
            return BidResult::LevelLocked;

        std::lock_guard<std::recursive_mutex> guard(sLock);
        auto itr = sAuctions.find(auctionId);
        if (itr == sAuctions.end())
            return BidResult::NotFound;

        Auction& a = itr->second;
        time_t now = Now();
        if (a.endTime <= now)
            return BidResult::Ended;
        if (a.ipState > PlayerState(player))
            return BidResult::EraLocked;
        if (sConfig.refuseKnown && KnowsItem(player, a.item))
            return BidResult::AlreadyKnown;

        ObjectGuid::LowType me = player->GetGUID().GetCounter();
        if (a.bidder == me)
            return BidResult::AlreadyHighBidder;

        uint32 next = NextBid(a);
        if (next > MAX_MONEY_AMOUNT || bid > MAX_MONEY_AMOUNT)
            return BidResult::TooHigh;
        if (bid < next)
            return BidResult::TooLow;
        if (!player->HasEnoughMoney(bid))
            return BidResult::NotEnoughMoney;

        Auction previous = a;
        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        RefundBid(previous, trans);

        player->ModifyMoney(-int32(bid));
        a.currentBid = bid;
        a.bidder = me;
        ++a.numBids;
        // A bid in the last minutes buys everybody time to answer it.
        if (sConfig.snipeMinutes && a.endTime - now < time_t(sConfig.snipeMinutes) * MINUTE)
            a.endTime = now + time_t(sConfig.snipeMinutes) * MINUTE;

        player->SaveInventoryAndGoldToDB(trans);
        SaveAuction(a, trans);
        CharacterDatabase.CommitTransaction(trans);

        LOG_INFO("module", "mod-black-market: {} ({}) bid {} copper on auction {} ({})",
            player->GetName(), me, bid, a.id, ItemName(a.item));

        if (previous.bidder && previous.bidder != me)
        {
            Tell(previous.bidder, Acore::StringFormat("|cff9d9d9dBlack Market:|r you have been outbid on {}. Your gold is in the mail.",
                ItemLink(a.item)));
            Addon::NotifyOutbid(previous.bidder, a.id);
        }
        Addon::NotifyChanged();
        return BidResult::Ok;
    }

    std::string BidResultText(BidResult result)
    {
        switch (result)
        {
            case BidResult::Ok:                return "Your bid is in. Keep it quiet.";
            case BidResult::Disabled:          return "The Black Market is closed.";
            case BidResult::NotFound:          return "That item is no longer for sale.";
            case BidResult::Ended:             return "Bidding on that item has ended.";
            case BidResult::TooLow:            return "Your bid is too low.";
            case BidResult::AlreadyHighBidder: return "You already hold the highest bid.";
            case BidResult::NotEnoughMoney:    return "You don't have enough money.";
            case BidResult::LevelLocked:       return Acore::StringFormat("Come back when you are level {}.", sConfig.minLevel);
            case BidResult::EraLocked:         return "That is not for the likes of you. Not yet.";
            case BidResult::AlreadyKnown:      return "You already have one of those.";
            case BidResult::TooFar:            return "Bids are taken in person. Go see Mister Chu.";
            case BidResult::TooHigh:           return "Nobody can pay that much.";
            case BidResult::SelfFound:         return "Swore to find your own, did you? Then go find it. I don't sell to your kind.";
        }
        return "Something went wrong.";
    }

    void SetBroker(Player* player, ObjectGuid broker)
    {
        std::lock_guard<std::recursive_mutex> guard(sLock);
        sBrokers[player->GetGUID().GetCounter()] = broker;
    }

    void ForgetPlayer(Player* player)
    {
        std::lock_guard<std::recursive_mutex> guard(sLock);
        sBrokers.erase(player->GetGUID().GetCounter());
    }

    std::string ItemName(uint32 item)
    {
        if (ItemTemplate const* proto = sObjectMgr->GetItemTemplate(item))
            return proto->Name1;
        return Acore::StringFormat("item {}", item);
    }

    std::string ItemLink(uint32 item)
    {
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(item);
        if (!proto)
            return ItemName(item);
        uint32 color = ItemQualityColors[std::min<uint32>(proto->Quality, MAX_ITEM_QUALITY - 1)];
        return Acore::StringFormat("|c{:08x}|Hitem:{}:0:0:0:0:0:0:0:0|h[{}]|h|r", color, item, proto->Name1);
    }

    std::string FormatMoney(uint32 copper)
    {
        uint32 gold = copper / GOLD, silver = (copper % GOLD) / SILVER, rest = copper % SILVER;
        std::string text;
        if (gold)
        {
            std::string digits = std::to_string(gold);
            for (int i = int(digits.size()) - 3; i > 0; i -= 3)
                digits.insert(std::size_t(i), ",");
            text = digits + "g";
        }
        if (silver)
            text += (text.empty() ? "" : " ") + std::to_string(silver) + "s";
        if (rest || text.empty())
            text += (text.empty() ? "" : " ") + std::to_string(rest) + "c";
        return text;
    }

    char const* TimeLeftText(uint8 timeLeft)
    {
        switch (timeLeft)
        {
            case TIME_LEFT_SHORT:     return "Short";
            case TIME_LEFT_MEDIUM:    return "Medium";
            case TIME_LEFT_LONG:      return "Long";
            case TIME_LEFT_VERY_LONG: return "Very Long";
            default:                  return "Ended";
        }
    }
}
