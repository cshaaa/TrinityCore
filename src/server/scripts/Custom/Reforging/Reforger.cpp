#include "Reforging.h"
#include <sstream>
#include <string>
#include <vector>
#include "Creature.h"
#include "Define.h"
#include "EventProcessor.h"
#include "GossipDef.h"
#include "Item.h"
#include "ItemPrototype.h"
#include "ObjectGuid.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptedGossip.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "Spell.h"
#include "Transaction.h"
#include "WorldPacket.h"
#include "WorldSession.h"

/*
Reforging by Rochet2
http://rochet2.github.io/

Rules of thumb:
Item can be reforged once.
Item reforge wont show to anyone but you in tooltips. Stats will be there nevertheless.
You will see the increased stats on all tooltips of the same item you reforged.
You can disable the stat changes to tooltips by setting send_cache_packets to false in Reforging.cpp.
Reforges are stripped when you mail, ah, guildbank the item etc. Only YOU can have the reforge.
Only item base stats are reforgable. Enchants and random stats are not.

This script is made blizzlike. This means that the reforgable stats etc are from CATACLYSM!
I have been informed that some stats were removed etc that would be important to be reforgable.
However I do not know what those stats are currently. Do look through the statTypes to add whatever you want.
Edit IsReforgable is you want to tweak requirements

*/

// Remember to add to GetStatName too
static const ItemModType statTypes[] = { ITEM_MOD_SPIRIT, ITEM_MOD_DODGE_RATING, ITEM_MOD_PARRY_RATING, ITEM_MOD_HIT_RATING, ITEM_MOD_CRIT_RATING, ITEM_MOD_HASTE_RATING, ITEM_MOD_EXPERTISE_RATING };
static const uint8 stat_type_max = sizeof(statTypes) / sizeof(*statTypes);

static const char* GetStatName(uint32 ItemStatType)
{
    switch(ItemStatType)
    {
    case ITEM_MOD_SPIRIT                   	: return "精神"; break;
    case ITEM_MOD_DODGE_RATING             	: return "闪避"; break;
    case ITEM_MOD_PARRY_RATING             	: return "招架"; break;
    case ITEM_MOD_HIT_RATING               	: return "命中等级"; break;
    case ITEM_MOD_CRIT_RATING              	: return "暴击等级"; break;
    case ITEM_MOD_HASTE_RATING             	: return "急速等级"; break;
    case ITEM_MOD_EXPERTISE_RATING         	: return "精准等级"; break;
    default: return NULL;
    }
}

static const char* GetSlotName(uint8 slot, WorldSession* /*session*/)
{
    switch (slot)
    {
    case EQUIPMENT_SLOT_HEAD      : return "头部";
    case EQUIPMENT_SLOT_NECK      : return "项链";
    case EQUIPMENT_SLOT_SHOULDERS : return "肩部";
    case EQUIPMENT_SLOT_BODY      : return "衬衫";
    case EQUIPMENT_SLOT_CHEST     : return "胸部";
    case EQUIPMENT_SLOT_WAIST     : return "腰带";
    case EQUIPMENT_SLOT_LEGS      : return "腿部";
    case EQUIPMENT_SLOT_FEET      : return "鞋子";
    case EQUIPMENT_SLOT_WRISTS    : return "手腕";
    case EQUIPMENT_SLOT_HANDS     : return "手套";
    case EQUIPMENT_SLOT_FINGER1   : return "右戒指";
    case EQUIPMENT_SLOT_FINGER2   : return "左戒指";
    case EQUIPMENT_SLOT_TRINKET1  : return "右饰品";
    case EQUIPMENT_SLOT_TRINKET2  : return "左饰品";
    case EQUIPMENT_SLOT_BACK      : return "背部";
    case EQUIPMENT_SLOT_MAINHAND  : return "主手";
    case EQUIPMENT_SLOT_OFFHAND   : return "副手";
    case EQUIPMENT_SLOT_TABARD    : return "战袍";
    case EQUIPMENT_SLOT_RANGED    : return "远程";
    default: return NULL;
    }
}

static uint32 Melt(uint8 i, uint8 j)
{
    return (i << 8) + j;
}

static void Unmelt(uint32 melt, uint8& i, uint8& j)
{
    i = melt >> 8;
    j = melt & 0xFF;
}

static Item* GetEquippedItem(Player* player, uint32 guidlow)
{
    for (uint8 i = EQUIPMENT_SLOT_START; i < INVENTORY_SLOT_ITEM_END; ++i)
        if (Item* pItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            if (pItem->GetGUID().GetCounter() == guidlow)
                return pItem;
    return NULL;
}

static bool IsReforgable(Item* invItem, Player* player)
{
    //if (!invItem->IsEquipped())
    //    return false;
    if (invItem->GetOwnerGUID() != player->GetGUID())
        return false;
    const ItemTemplate* pProto = invItem->GetTemplate();
    //if (pProto->ItemLevel < 200)
    //    return false;
    //if (pProto->Quality == ITEM_QUALITY_HEIRLOOM) // block heirlooms necessary?
    //    return false;
    if (!pProto->StatsCount || pProto->StatsCount >= MAX_ITEM_PROTO_STATS) // Mandatory! Do NOT remove or edit
        return false;
    if (!player->reforgeMap.empty() && player->reforgeMap.find(invItem->GetGUID().GetCounter()) != player->reforgeMap.end()) // Mandatory! Do NOT remove or edit
        return false;
    for (uint32 i = 0; i < pProto->StatsCount; ++i)
    {
        if (!GetStatName(pProto->ItemStat[i].ItemStatType))
            continue;
        if (((int32)floorf((float)pProto->ItemStat[i].ItemStatValue * 0.4f)) > 1)
            return true;
    }
    return false;
}

static void UpdatePlayerReforgeStats(Item* invItem, Player* player, uint32 decrease, uint32 increase) // stat types
{
    const ItemTemplate* pProto = invItem->GetTemplate();

    int32 stat_diff = 0;
    for (uint32 i = 0; i < pProto->StatsCount; ++i)
    {
        if (pProto->ItemStat[i].ItemStatType == increase)
            return; // Should not have the increased stat already
        if (pProto->ItemStat[i].ItemStatType == decrease)
            stat_diff = (int32)floorf((float)pProto->ItemStat[i].ItemStatValue * 0.4f);
    }
    if (stat_diff <= 0)
        return; // Should have some kind of diff

    // Update player stats
    player->_ApplyItemMods(invItem, invItem->GetSlot(), false);
    uint32 guidlow = invItem->GetGUID().GetCounter();
    ReforgeData& data = player->reforgeMap[guidlow];
    data.increase = increase;
    data.decrease = decrease;
    data.stat_value = stat_diff;
    player->_ApplyItemMods(invItem, invItem->GetSlot(), true);
    // CharacterDatabase.PExecute("REPLACE INTO `custom_reforging` (`GUID`, `increase`, `decrease`, `stat_value`) VALUES (%u, %u, %u, %i)", guidlow, increase, decrease, stat_diff);
    player->ModifyMoney(pProto->SellPrice < (10*GOLD) ? (-10*GOLD) : -(int32)pProto->SellPrice);
    SendReforgePacket(player, invItem->GetEntry(), 0, &data);
    // player->SaveToDB();
}

class REFORGE_PLAYER : public PlayerScript
{
public:
    REFORGE_PLAYER() : PlayerScript("REFORGE_PLAYER")
    {
        CharacterDatabase.DirectExecute("DELETE FROM `custom_reforging` WHERE NOT EXISTS (SELECT 1 FROM `item_instance` WHERE `item_instance`.`guid` = `custom_reforging`.`GUID`)");
    }

    class SendRefPackLogin : public BasicEvent
    {
    public:
        SendRefPackLogin(Player* _player) : player(_player)
        {
            _player->m_Events.AddEvent(this, _player->m_Events.CalculateTime(1000));
        }

        bool Execute(uint64, uint32) override
        {
            SendReforgePackets(player);
            return true;
        }
        Player* player;
    };

    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
        uint32 playerGUID = player->GetGUID().GetCounter();
        QueryResult result = CharacterDatabase.PQuery("SELECT `GUID`, `increase`, `decrease`, `stat_value` FROM `custom_reforging` WHERE `Owner` = %u", playerGUID);
        if (result)
        {
            do
            {
                uint32 lowGUID = (*result)[0].GetUInt32();
                Item* invItem = player->GetItemByGuid(ObjectGuid(HighGuid::Item, 0, lowGUID));
                if (invItem)
                    player->_ApplyItemMods(invItem, invItem->GetSlot(), false);
                ReforgeData& data = player->reforgeMap[lowGUID];
                data.increase = (*result)[1].GetUInt32();
                data.decrease = (*result)[2].GetUInt32();
                data.stat_value = (*result)[3].GetInt32();
                if (invItem)
                    player->_ApplyItemMods(invItem, invItem->GetSlot(), true);
                // SendReforgePacket(player, entry, lowGUID);
            } while (result->NextRow());

            // SendReforgePackets(player);
            new SendRefPackLogin(player);
        }
    }

    //void OnLogout(Player* player) override
    //{
    //    if (player->reforgeMap.empty())
    //        return;
    //    for (ReforgeMapType::const_iterator it = player->reforgeMap.begin(); it != player->reforgeMap.end();)
    //    {
    //        ReforgeMapType::const_iterator old_it = it++;
    //        RemoveReforge(player, old_it->first, false);
    //    }
    //}

    void OnSave(Player* player) override
    {
        uint32 lowguid = player->GetGUID().GetCounter();
        SQLTransaction trans = CharacterDatabase.BeginTransaction();
        trans->PAppend("DELETE FROM `custom_reforging` WHERE `Owner` = %u", lowguid);
        if (!player->reforgeMap.empty())
        {
            // Only save items that are in inventory / bank / etc
            std::vector<Item*> items = GetItemList(player);
            for (std::vector<Item*>::const_iterator it = items.begin(); it != items.end(); ++it)
            {
                ReforgeMapType::const_iterator it2 = player->reforgeMap.find((*it)->GetGUID().GetCounter());
                if (it2 == player->reforgeMap.end())
                    continue;

                const ReforgeData& data = it2->second;
                trans->PAppend("REPLACE INTO `custom_reforging` (`GUID`, `increase`, `decrease`, `stat_value`, `Owner`) VALUES (%u, %u, %u, %i, %u)", it2->first, data.increase, data.decrease, data.stat_value, lowguid);
            }
        }

        if (trans->GetSize()) // basically never false
            CharacterDatabase.CommitTransaction(trans);
    }
};

class REFORGER_NPC : public CreatureScript
{
public:
    REFORGER_NPC() : CreatureScript("REFORGER_NPC") { }

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        player->ADD_GOSSIP_ITEM(GOSSIP_ICON_BATTLE, "选择一个物品重铸:", 0, Melt(MAIN_MENU, 0));
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        {
            if (Item* invItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                if (IsReforgable(invItem, player))
                    if (const char* slotname = GetSlotName(slot, player->GetSession()))
                        player->ADD_GOSSIP_ITEM(GOSSIP_ICON_TRAINER, slotname, 0, Melt(SELECT_STAT_REDUCE, slot));
        }
        player->ADD_GOSSIP_ITEM(GOSSIP_ICON_TRAINER, "移除重铸", 0, Melt(SELECT_RESTORE, 0));
        player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, "更新菜单", 0, Melt(MAIN_MENU, 0));
        player->SEND_GOSSIP_MENU(DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
        return true;
    }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 melt) override
    {
        player->PlayerTalkClass->ClearMenus();

        uint8 menu, action;
        Unmelt(melt, menu, action);

        switch (menu)
        {
        case MAIN_MENU: OnGossipHello(player, creature); break;
        case SELECT_STAT_REDUCE:
            // action = slot
            if (Item* invItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, action))
            {
                if (IsReforgable(invItem, player))
                {
                    uint32 guidlow = invItem->GetGUID().GetCounter();
                    const ItemTemplate* pProto = invItem->GetTemplate();
                    player->ADD_GOSSIP_ITEM(GOSSIP_ICON_BATTLE, "属性将减少:", sender, melt);
                    for (uint32 i = 0; i < pProto->StatsCount; ++i)
                    {
                        int32 stat_diff = ((int32)floorf((float)pProto->ItemStat[i].ItemStatValue * 0.4f));
                        if (stat_diff > 1)
                            if (const char* stat_name = GetStatName(pProto->ItemStat[i].ItemStatType))
                            {
                                std::ostringstream oss;
                                oss << stat_name << " (" << pProto->ItemStat[i].ItemStatValue << " |cFFDB2222-" << stat_diff << "|r)";
                                player->ADD_GOSSIP_ITEM(GOSSIP_ICON_TRAINER, oss.str(), guidlow, Melt(SELECT_STAT_INCREASE, i));
                            }
                    }
                    player->ADD_GOSSIP_ITEM(GOSSIP_ICON_TALK, "返回..", 0, Melt(MAIN_MENU, 0));
                    player->SEND_GOSSIP_MENU(DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
                }
                else
                {
                    player->GetSession()->SendNotification("物品选择错误");
                    OnGossipHello(player, creature);
                }
            }
            else
            {
                player->GetSession()->SendNotification("物品选择错误");
                OnGossipHello(player, creature);
            }
            break;
        case SELECT_STAT_INCREASE:
            // sender = item guidlow
            // action = StatsCount id
            {
                Item* invItem = GetEquippedItem(player, sender);
                if (invItem)
                {
                    const ItemTemplate* pProto = invItem->GetTemplate();
                    int32 stat_diff = ((int32)floorf((float)pProto->ItemStat[action].ItemStatValue * 0.4f));

                    player->ADD_GOSSIP_ITEM(GOSSIP_ICON_BATTLE, "属性将增加:", sender, melt);
                    for (uint8 i = 0; i < stat_type_max; ++i)
                    {
                        bool cont = false;
                        for (uint32 j = 0; j < pProto->StatsCount; ++j)
                        {
                            if (statTypes[i] == pProto->ItemStat[j].ItemStatType) // skip existing stats on item
                            {
                                cont = true;
                                break;
                            }
                        }
                        if (cont)
                            continue;
                        if (const char* stat_name = GetStatName(statTypes[i]))
                        {
                            std::ostringstream oss;
                            oss << stat_name << " |cFF3ECB3C+" << stat_diff << "|r";
                            player->ADD_GOSSIP_ITEM_EXTENDED(GOSSIP_ICON_INTERACT_1, oss.str(), sender, Melt(i, (uint8)pProto->ItemStat[action].ItemStatType), "确定重铸\n\n" + pProto->Name1, (pProto->SellPrice < (10 * GOLD) ? (10 * GOLD) : pProto->SellPrice), false);
                        }
                    }
                    player->ADD_GOSSIP_ITEM(GOSSIP_ICON_TALK, "返回..", 0, Melt(SELECT_STAT_REDUCE, invItem->GetSlot()));
                    player->SEND_GOSSIP_MENU(DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
                }
                else
                {
                    player->GetSession()->SendNotification("物品选择错误");
                    OnGossipHello(player, creature);
                }
            }
            break;
        case SELECT_RESTORE:
            {
                player->ADD_GOSSIP_ITEM(GOSSIP_ICON_BATTLE, "选择要移除重铸的物品:", sender, melt);
                if (!player->reforgeMap.empty())
                {
                    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
                    {
                        if (Item* invItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                            if (player->reforgeMap.find(invItem->GetGUID().GetCounter()) != player->reforgeMap.end())
                                if (const char* slotname = GetSlotName(slot, player->GetSession()))
                                    player->ADD_GOSSIP_ITEM_EXTENDED(GOSSIP_ICON_INTERACT_1, slotname, invItem->GetGUID().GetCounter(), Melt(RESTORE, 0), "移除重铸\n\n" + invItem->GetTemplate()->Name1, 0, false);
                    }
                }
                player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, "更新菜单", sender, melt);
                player->ADD_GOSSIP_ITEM(GOSSIP_ICON_TALK, "返回..", 0, Melt(MAIN_MENU, 0));
                player->SEND_GOSSIP_MENU(DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
            }
            break;
        case RESTORE:
            // sender = item guidlow
            {
                if (player->GetItemByGuid(ObjectGuid(HighGuid::Item, 0, sender)))
                {
                    if (!player->reforgeMap.empty() && player->reforgeMap.find(sender) != player->reforgeMap.end())
                        RemoveReforge(player, sender, true);
                }
                OnGossipHello(player, creature);
            }
            break;
        default: // Reforge
            // sender = item guidlow
            // menu = stat type to increase index to statTypes[]
            // action = stat type to decrease
            {
                if (menu < stat_type_max)
                {
                    Item* invItem = GetEquippedItem(player, sender);
                    if (invItem && IsReforgable(invItem, player))
                    {
                        if (player->HasEnoughMoney(invItem->GetTemplate()->SellPrice < (10 * GOLD) ? (10 * GOLD) : invItem->GetTemplate()->SellPrice))
                        {
                            // int32 stat_diff = ((int32)floorf((float)invItem->GetTemplate()->ItemStat[action].ItemStatValue * 0.4f));
                            UpdatePlayerReforgeStats(invItem, player, action, statTypes[menu]); // rewrite this function
                        }
                        else
                        {
                            player->GetSession()->SendNotification("金币不足");
                        }
                    }
                    else
                    {
                        player->GetSession()->SendNotification("物品选择错误");
                    }
                }
                OnGossipHello(player, creature);
            }
        }
        return true;
    }

    enum Menus
    {
        MAIN_MENU = 200, // stat_type_max
        SELECT_ITEM,
        SELECT_STAT_REDUCE,
        SELECT_STAT_INCREASE,
        SELECT_RESTORE,
        RESTORE,
        REFORGE,
    };
};

void AddSC_REFORGER_NPC()
{
    new REFORGER_NPC;
    new REFORGE_PLAYER;
}
