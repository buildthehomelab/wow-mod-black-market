/*
 * mod-black-market: the BlackMarket addon's channel. The addon whispers itself with the prefix
 * BMAH; the server swallows those whispers and answers the same way.
 *
 *   addon -> server   HELLO:<protocol>   LIST   BID:<auction>:<copper>
 *   server -> addon   OPEN   LB:<count>   L:<row>;<row>...   LE:<min level>
 *                     BID:OK:<auction>   BID:ERR:<auction>:<text>   OUTBID:<auction>   CHG
 *
 * A row is id,item,quantity,next bid,current bid,bids,time left,flags,progression state,seller.
 *
 * Released under the MIT License.
 */

#include "BlackMarket.h"
#include "Chat.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "Timer.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include <charconv>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace BlackMarket::Addon
{
    namespace
    {
        constexpr char const* PREFIX = "BMAH";
        constexpr std::size_t MAX_PAYLOAD = 240;
        constexpr uint32 LIST_COOLDOWN_MS = 1000;
        constexpr uint32 BID_COOLDOWN_MS = 500;

        std::mutex sLock;
        std::unordered_set<ObjectGuid::LowType> sUsers;
        std::unordered_map<ObjectGuid::LowType, uint32> sLastList;
        std::unordered_map<ObjectGuid::LowType, uint32> sLastBid;

        void Send(Player* player, std::string const& payload)
        {
            if (!player || !player->GetSession())
                return;
            std::string full = std::string(PREFIX) + "\t" + payload;
            WorldPacket data;
            ChatHandler::BuildChatPacket(data, CHAT_MSG_WHISPER, LANG_ADDON, player, player, full);
            player->GetSession()->SendPacket(&data);
        }

        bool ParseUInt(std::string_view text, uint32& out)
        {
            if (text.empty())
                return false;
            auto result = std::from_chars(text.data(), text.data() + text.size(), out, 10);
            return result.ec == std::errc{} && result.ptr == text.data() + text.size();
        }

        // Too soon since this player's last request of this kind?
        bool Throttled(std::unordered_map<ObjectGuid::LowType, uint32>& last, ObjectGuid::LowType who, uint32 cooldown)
        {
            std::lock_guard<std::mutex> guard(sLock);
            uint32 now = getMSTime();
            auto itr = last.find(who);
            if (itr != last.end() && getMSTimeDiff(itr->second, now) < cooldown)
                return true;
            last[who] = now;
            return false;
        }

        void HandleBid(Player* player, std::string_view args)
        {
            std::size_t colon = args.find(':');
            uint32 auctionId = 0, bid = 0;
            if (colon == std::string_view::npos || !ParseUInt(args.substr(0, colon), auctionId) || !ParseUInt(args.substr(colon + 1), bid))
                return;

            if (Throttled(sLastBid, player->GetGUID().GetCounter(), BID_COOLDOWN_MS))
            {
                Send(player, Acore::StringFormat("BID:ERR:{}:Slow down.", auctionId));
                return;
            }

            BidResult result = PlaceBid(player, auctionId, bid);
            if (result == BidResult::Ok)
                Send(player, Acore::StringFormat("BID:OK:{}", auctionId));
            else
                Send(player, Acore::StringFormat("BID:ERR:{}:{}", auctionId, BidResultText(result)));
            SendList(player);
        }

        void Dispatch(Player* player, std::string_view msg)
        {
            std::size_t colon = msg.find(':');
            std::string_view cmd = msg.substr(0, colon);
            std::string_view args = colon == std::string_view::npos ? std::string_view() : msg.substr(colon + 1);

            if (cmd == "HELLO")
            {
                std::lock_guard<std::mutex> guard(sLock);
                sUsers.insert(player->GetGUID().GetCounter());
            }
            else if (cmd == "LIST")
            {
                if (!Throttled(sLastList, player->GetGUID().GetCounter(), LIST_COOLDOWN_MS))
                    SendList(player);
            }
            else if (cmd == "BID")
                HandleBid(player, args);
        }
    }

    bool HasAddon(Player* player)
    {
        std::lock_guard<std::mutex> guard(sLock);
        return sUsers.count(player->GetGUID().GetCounter()) != 0;
    }

    void Open(Player* player)
    {
        Send(player, "OPEN");
        SendList(player);
    }

    void SendList(Player* player)
    {
        std::vector<Listing> listings = GetListings(player);
        Send(player, Acore::StringFormat("LB:{}", listings.size()));

        std::string line;
        for (Listing const& l : listings)
        {
            std::string row = Acore::StringFormat("{},{},{},{},{},{},{},{},{},{}", l.id, l.item, l.quantity, l.nextBid,
                l.currentBid, l.numBids, l.timeLeft, l.flags, l.ipState, l.seller.substr(0, 48));
            if (!line.empty() && 2 + line.size() + 1 + row.size() > MAX_PAYLOAD)
            {
                Send(player, "L:" + line);
                line.clear();
            }
            if (!line.empty())
                line += ';';
            line += row;
        }
        if (!line.empty())
            Send(player, "L:" + line);

        Send(player, Acore::StringFormat("LE:{}", GetConfig().minLevel));
    }

    void NotifyOutbid(ObjectGuid::LowType playerLow, uint32 auctionId)
    {
        if (Player* player = ObjectAccessor::FindConnectedPlayer(ObjectGuid::Create<HighGuid::Player>(playerLow)))
            if (HasAddon(player))
                Send(player, Acore::StringFormat("OUTBID:{}", auctionId));
    }

    void NotifyChanged()
    {
        std::vector<ObjectGuid::LowType> users;
        {
            std::lock_guard<std::mutex> guard(sLock);
            users.assign(sUsers.begin(), sUsers.end());
        }
        for (ObjectGuid::LowType low : users)
            if (Player* player = ObjectAccessor::FindConnectedPlayer(ObjectGuid::Create<HighGuid::Player>(low)))
                Send(player, "CHG");
    }

    void Forget(Player* player)
    {
        std::lock_guard<std::mutex> guard(sLock);
        ObjectGuid::LowType low = player->GetGUID().GetCounter();
        sUsers.erase(low);
        sLastList.erase(low);
        sLastBid.erase(low);
    }

    class BlackMarketAddonPlayerScript : public PlayerScript
    {
    public:
        BlackMarketAddonPlayerScript() : PlayerScript("BlackMarketAddonPlayerScript",
            {
                PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT,
                PLAYERHOOK_ON_LOGOUT
            }) { }

        // The addon whispers itself; swallow those so they never show up as chat.
        bool OnPlayerCanUseChat(Player* player, uint32 /*type*/, uint32 lang, std::string& msg, Player* receiver) override
        {
            if (lang != LANG_ADDON || !receiver || receiver != player)
                return true;

            std::string const prefixTab = std::string(PREFIX) + "\t";
            if (msg.compare(0, prefixTab.size(), prefixTab) != 0)
                return true;

            if (!GetConfig().enabled)
                return false;

            Dispatch(player, std::string_view(msg).substr(prefixTab.size()));
            return false;
        }

        void OnPlayerLogout(Player* player) override
        {
            Forget(player);
            ForgetPlayer(player);
        }
    };
}

void AddBlackMarketAddonScripts()
{
    new BlackMarket::Addon::BlackMarketAddonPlayerScript();
}
