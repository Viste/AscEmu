/*
 * AscEmu Framework based on ArcEmu MMORPG Server
 * Copyright (c) 2014-2021 AscEmu Team <http://www.ascemu.org>
 * Copyright (C) 2008-2012 ArcEmu Team <http://www.ArcEmu.org/>
 * Copyright (C) 2005-2007 Ascent Team
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */


#include "Management/QuestLogEntry.hpp"
#include "Objects/Container.h"
#include "Exceptions/Exceptions.hpp"
#include "Objects/Units/Stats.h"
#include "Management/ArenaTeam.h"
#include "Storage/MySQLDataStore.hpp"
#include "Storage/MySQLStructures.h"
#include "Objects/Units/Players/PlayerClasses.hpp"
#include "Server/MainServerDefines.h"
#include "Map/InstanceDefines.hpp"
#include "Map/MapMgr.h"
#include "Map/MapScriptInterface.h"
#include "Spell/SpellMgr.hpp"
#include "Objects/Units/Creatures/Pet.h"
#include "Spell/Definitions/SpellEffects.hpp"
#include "Management/TaxiMgr.h"
#include "Management/LFG/LFGMgr.hpp"
#include "Movement/MovementManager.h"
#include "Objects/Units/Creatures/Summons/Summon.h"
#include "Util/Strings.hpp"
#if VERSION_STRING < Cata
#include "Management/Guild/Guild.hpp"
#endif

const char* NormalTalkMessage = "DMSG";

ObjectMgr& ObjectMgr::getInstance()
{
    static ObjectMgr mInstance;
    return mInstance;
}

void ObjectMgr::initialize()
{
    m_hiItemGuid = 0;
    m_hiGroupId = 0;
    m_mailid = 0;
    m_reportID = 0;
    m_setGUID = 0;
    m_hiCorpseGuid = 0;
    m_hiGuildId = 0;
    m_hiPetGuid = 0;
    m_hiArenaTeamId = 0;
    m_hiPlayerGuid = 1;
#if VERSION_STRING > WotLK
    m_voidItemId = 1;
#endif
}

void ObjectMgr::finalize()
{
    sLogger.info("ObjectMgr : Deleting Corpses...");
    CorpseCollectorUnload();

    sLogger.info("ObjectMgr : Deleting Vendors...");
    for (VendorMap::iterator i = mVendors.begin(); i != mVendors.end(); ++i)
    {
        delete i->second;
    }

    sLogger.info("ObjectMgr : Deleting Trainers...");
    for (TrainerMap::iterator i = mTrainers.begin(); i != mTrainers.end(); ++i)
    {
        Trainer* t = i->second;
        if (t->UIMessage && t->UIMessage != (char*)NormalTalkMessage)
            delete[] t->UIMessage;
        delete t;
    }

    sLogger.info("ObjectMgr : Deleting Level Information...");
    for (LevelInfoMap::iterator i = mLevelInfo.begin(); i != mLevelInfo.end(); ++i)
    {
        LevelMap* l = i->second;
        for (LevelMap::iterator i2 = l->begin(); i2 != l->end(); ++i2)
        {
            delete i2->second;
        }

        l->clear();
        delete l;
    }

    sLogger.info("ObjectMgr : Deleting timed emote Cache...");
    for (std::unordered_map<uint32, TimedEmoteList*>::iterator i = m_timedemotes.begin(); i != m_timedemotes.end(); ++i)
    {
        for (TimedEmoteList::iterator i2 = i->second->begin(); i2 != i->second->end(); ++i2)
            if ((*i2))
            {
                delete[](*i2)->msg;
                delete(*i2);
            }

        delete i->second;
    }

    sLogger.info("ObjectMgr : Deleting Charters...");
    for (uint8 i = 0; i < NUM_CHARTER_TYPES; ++i)
    {
        for (std::unordered_map<uint32, Charter*>::iterator itr = m_charters[i].begin(); itr != m_charters[i].end(); ++itr)
        {
            delete itr->second;
        }
    }

    sLogger.info("ObjectMgr : Deleting Reputation Tables...");
    for (ReputationModMap::iterator itr = m_reputation_creature.begin(); itr != m_reputation_creature.end(); ++itr)
    {
        ReputationModifier* mod = itr->second;
        mod->mods.clear();
        delete mod;
    }
    for (ReputationModMap::iterator itr = m_reputation_faction.begin(); itr != m_reputation_faction.end(); ++itr)
    {
        ReputationModifier* mod = itr->second;
        mod->mods.clear();
        delete mod;
    }

    for (std::unordered_map<uint32, InstanceReputationModifier*>::iterator itr = this->m_reputation_instance.begin(); itr != this->m_reputation_instance.end(); ++itr)
    {
        InstanceReputationModifier* mod = itr->second;
        mod->mods.clear();
        delete mod;
    }

    sLogger.info("ObjectMgr : Deleting Groups...");
    for (GroupMap::iterator itr = m_groups.begin(); itr != m_groups.end();)
    {
        Group* pGroup = itr->second;
        ++itr;
        if (pGroup != nullptr)
        {
            for (uint32 i = 0; i < pGroup->GetSubGroupCount(); ++i)
            {
                SubGroup* pSubGroup = pGroup->GetSubGroup(i);
                if (pSubGroup != nullptr)
                {
                    pSubGroup->Disband();
                }
            }
            delete pGroup;
        }
    }

    sLogger.info("ObjectMgr : Deleting Player Information...");
    for (std::unordered_map<uint32, CachedCharacterInfo*>::iterator itr = m_playersinfo.begin(); itr != m_playersinfo.end(); ++itr)
    {
        itr->second->m_Group = nullptr;
        delete itr->second;
    }

    sLogger.info("ObjectMgr : Deleting Boss Information...");
    for (DungeonEncounterContainer::iterator itr = _dungeonEncounterStore.begin(); itr != _dungeonEncounterStore.end(); ++itr)
        for (DungeonEncounterList::iterator encounterItr = itr->second.begin(); encounterItr != itr->second.end(); ++encounterItr)
            delete *encounterItr;

    sLogger.info("ObjectMgr : Deleting Arena Teams...");
    for (std::unordered_map<uint32, ArenaTeam*>::iterator itr = m_arenaTeams.begin(); itr != m_arenaTeams.end(); ++itr)
    {
        delete(*itr).second;
    }

    sLogger.info("ObjectMgr : Cleaning up spell target constraints...");
    for (SpellTargetConstraintMap::iterator itr = m_spelltargetconstraints.begin(); itr != m_spelltargetconstraints.end(); ++itr)
        delete itr->second;

    m_spelltargetconstraints.clear();

    sLogger.info("ObjectMgr : Cleaning up vehicle accessories...");
    for (std::map< uint32, std::vector< VehicleAccessoryEntry* >* >::iterator itr = vehicle_accessories.begin(); itr != vehicle_accessories.end(); ++itr)
    {
        std::vector< VehicleAccessoryEntry* > *v = itr->second;

        for (std::vector< VehicleAccessoryEntry* >::iterator itr2 = v->begin(); itr2 != v->end(); ++itr2)
            delete *itr2;
        v->clear();

        delete v;
    }

    vehicle_accessories.clear();


    sLogger.info("ObjectMgr : Cleaning up worldstate templates...");
    for (std::map< uint32, std::multimap< uint32, WorldState >* >::iterator itr = worldstate_templates.begin(); itr != worldstate_templates.end(); ++itr)
    {
        itr->second->clear();
        delete itr->second;
    }

    worldstate_templates.clear();


    sLogger.info("ObjectMgr : Clearing up event scripts...");
    mEventScriptMaps.clear();
    mSpellEffectMaps.clear();
}

Player* ObjectMgr::createPlayerByGuid(uint8_t _class, uint32_t guid)
{
    Player* player;

    switch (_class)
    {
        case WARRIOR:
            player = new Warrior(guid);
            break;
        case PALADIN:
            player = new Paladin(guid);
            break;
        case HUNTER:
            player = new Hunter(guid);
            break;
        case ROGUE:
            player = new Rogue(guid);
            break;
        case PRIEST:
            player = new Priest(guid);
            break;
#if VERSION_STRING > TBC
        case DEATHKNIGHT:
            player = new DeathKnight(guid);
            break;
#endif
        case SHAMAN:
            player = new Shaman(guid);
            break;
        case MAGE:
            player = new Mage(guid);
            break;
        case WARLOCK:
            player = new Warlock(guid);
            break;
#if VERSION_STRING > Cata
        case MONK:
            player = new Monk(guid);
            break;
#endif
        case DRUID:
            player = new Druid(guid);
            break;
        default:
            player = nullptr;
            break;
    }

    return player;
}

GameObject* ObjectMgr::createGameObjectByGuid(uint32_t id, uint32_t guid)
{
    GameObjectProperties const* gameobjectProperties = sMySQLStore.getGameObjectProperties(id);
    if (gameobjectProperties == nullptr)
        return nullptr;

    GameObject* gameObject;

    const uint64_t createdGuid = uint64_t((uint64_t(HIGHGUID_TYPE_GAMEOBJECT) << 32) | guid);

    switch (gameobjectProperties->type)
    {
        case GAMEOBJECT_TYPE_DOOR:
            gameObject = new GameObject_Door(createdGuid);
            break;
        case GAMEOBJECT_TYPE_BUTTON:
            gameObject = new GameObject_Button(createdGuid);
            break;
        case GAMEOBJECT_TYPE_QUESTGIVER:
            gameObject = new GameObject_QuestGiver(createdGuid);
            break;
        case GAMEOBJECT_TYPE_CHEST:
            gameObject = new GameObject_Chest(createdGuid);
            break;
        case GAMEOBJECT_TYPE_TRAP:
            gameObject = new GameObject_Trap(createdGuid);
            break;
        case GAMEOBJECT_TYPE_CHAIR:
            gameObject = new GameObject_Chair(createdGuid);
            break;
        case GAMEOBJECT_TYPE_SPELL_FOCUS:
            gameObject = new GameObject_SpellFocus(createdGuid);
            break;
        case GAMEOBJECT_TYPE_GOOBER:
            gameObject = new GameObject_Goober(createdGuid);
            break;
        case GAMEOBJECT_TYPE_CAMERA:
            gameObject = new GameObject_Camera(createdGuid);
            break;
        case GAMEOBJECT_TYPE_FISHINGNODE:
            gameObject = new GameObject_FishingNode(createdGuid);
            break;
        case GAMEOBJECT_TYPE_RITUAL:
            gameObject = new GameObject_Ritual(createdGuid);
            break;
        case GAMEOBJECT_TYPE_SPELLCASTER:
            gameObject = new GameObject_SpellCaster(createdGuid);
            break;
        case GAMEOBJECT_TYPE_MEETINGSTONE:
            gameObject = new GameObject_Meetingstone(createdGuid);
            break;
        case GAMEOBJECT_TYPE_FLAGSTAND:
            gameObject = new GameObject_FlagStand(createdGuid);
            break;
        case GAMEOBJECT_TYPE_FISHINGHOLE:
            gameObject = new GameObject_FishingHole(createdGuid);
            break;
        case GAMEOBJECT_TYPE_FLAGDROP:
            gameObject = new GameObject_FlagDrop(createdGuid);
            break;
        case GAMEOBJECT_TYPE_BARBER_CHAIR:
            gameObject = new GameObject_BarberChair(createdGuid);
            break;
        case GAMEOBJECT_TYPE_DESTRUCTIBLE_BUILDING:
            gameObject = new GameObject_Destructible(createdGuid);
            break;
        default:
            gameObject = new GameObject(createdGuid);
            break;
    }

    gameObject->SetGameObjectProperties(gameobjectProperties);

    return gameObject;
}

Summon* ObjectMgr::createSummonByGuid(uint64_t guid, SummonType type, uint32_t duration)
{
    Summon* summon;

    switch (type)
    {
        case SUMMONTYPE_GUARDIAN:
            summon = new GuardianSummon(guid, duration);
            break;
        case SUMMONTYPE_WILD:
            summon = new WildSummon(guid, duration);
            break;
        case SUMMONTYPE_TOTEM:
            summon = new TotemSummon(guid, duration);
            break;
        case SUMMONTYPE_COMPANION:
            summon = new CompanionSummon(guid, duration);
            break;
        case SUMMONTYPE_POSSESSED:
            summon = new PossessedSummon(guid, duration);
            break;
        default:
            summon = new Summon(guid, duration);
            break;
    }

    return summon;
}

//////////////////////////////////////////////////////////////////////////////////////////
// Groups
Group* ObjectMgr::GetGroupByLeader(Player* pPlayer)
{
    std::lock_guard<std::mutex> guard(m_groupLock);

    for (GroupMap::iterator itr = m_groups.begin(); itr != m_groups.end(); ++itr)
    {
        if (itr->second->GetLeader() == pPlayer->getPlayerInfo())
        {
            return itr->second;
        }
    }

    return nullptr;
}

Group* ObjectMgr::GetGroupById(uint32 id)
{
    std::lock_guard<std::mutex> guard(m_groupLock);

    GroupMap::iterator itr = m_groups.find(id);
    if (itr != m_groups.end())
        return itr->second;

    return nullptr;
}

//////////////////////////////////////////////////////////////////////////////////////////
// Player names
void ObjectMgr::DeletePlayerInfo(uint32 guid)
{
    std::lock_guard<std::mutex> guard(playernamelock);

    std::unordered_map<uint32, CachedCharacterInfo*>::iterator i = m_playersinfo.find(guid);
    if (i == m_playersinfo.end())
        return;

    CachedCharacterInfo* pl = i->second;
    if (pl->m_Group)
        pl->m_Group->RemovePlayer(pl);

    std::string pnam = pl->name;
    AscEmu::Util::Strings::toLowerCase(pnam);
    PlayerNameStringIndexMap::iterator i2 = m_playersInfoByName.find(pnam);
    if (i2 != m_playersInfoByName.end() && i2->second == pl)
    {
        m_playersInfoByName.erase(i2);
    }

    delete i->second;
    m_playersinfo.erase(i);
}

CachedCharacterInfo* ObjectMgr::GetPlayerInfo(uint32 guid)
{
    std::lock_guard<std::mutex> guard(playernamelock);

    std::unordered_map<uint32, CachedCharacterInfo*>::iterator i = m_playersinfo.find(guid);
    if (i != m_playersinfo.end())
        return i->second;

    return nullptr;
}

void ObjectMgr::AddPlayerInfo(CachedCharacterInfo* pn)
{
    std::lock_guard<std::mutex> guard(playernamelock);

    m_playersinfo[pn->guid] = pn;

    std::string pnam = pn->name;
    AscEmu::Util::Strings::toLowerCase(pnam);
    m_playersInfoByName[pnam] = pn;
}

void ObjectMgr::RenamePlayerInfo(CachedCharacterInfo* pn, std::string oldname, std::string newname)
{
    std::lock_guard<std::mutex> guard(playernamelock);

    std::string oldn = oldname;
    AscEmu::Util::Strings::toLowerCase(oldn);

    PlayerNameStringIndexMap::iterator itr = m_playersInfoByName.find(oldn);
    if (itr != m_playersInfoByName.end() && itr->second == pn)
    {
        std::string newn = newname;
        AscEmu::Util::Strings::toLowerCase(newn);
        m_playersInfoByName.erase(itr);
        m_playersInfoByName[newn] = pn;
    }
}

void ObjectMgr::LoadSpellSkills()
{
    for (uint32 i = 0; i < sSkillLineAbilityStore.GetNumRows(); i++)
    {
        auto skill_line_ability = sSkillLineAbilityStore.LookupEntry(i);
        if (skill_line_ability)
        {
            mSpellSkills[skill_line_ability->spell] = skill_line_ability;
        }
    }
    sLogger.info("ObjectMgr : %u spell skills loaded.", static_cast<uint32_t>(mSpellSkills.size()));
}

DBC::Structures::SkillLineAbilityEntry const* ObjectMgr::GetSpellSkill(uint32 id)
{
    return mSpellSkills[id];
}

SpellInfo const* ObjectMgr::GetNextSpellRank(SpellInfo const* sp, uint32 level)
{
    // Looks for next spell rank
    if (sp == nullptr)
    {
        return nullptr;
    }

    auto skill_line_ability = GetSpellSkill(sp->getId());
    if (skill_line_ability != nullptr && skill_line_ability->next > 0)
    {
        SpellInfo const* sp1 = sSpellMgr.getSpellInfo(skill_line_ability->next);
        if (sp1 && sp1->getBaseLevel() <= level)   // check level
        {
            return GetNextSpellRank(sp1, level);   // recursive for higher ranks
        }
    }
    return sp;
}

void ObjectMgr::LoadPlayersInfo()
{
    QueryResult* result = CharacterDatabase.Query("SELECT guid, name, race, class, level, gender, zoneid, timestamp, acct FROM characters");
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            CachedCharacterInfo* pn = new CachedCharacterInfo;
            pn->guid = fields[0].GetUInt32();
            std::string dbName = fields[1].GetString();
            AscEmu::Util::Strings::capitalize(dbName);
            pn->name = dbName;
            pn->race = fields[2].GetUInt8();
            pn->cl = fields[3].GetUInt8();
            pn->lastLevel = fields[4].GetUInt32();
            pn->gender = fields[5].GetUInt8();
            pn->lastZone = fields[6].GetUInt32();
            pn->lastOnline = fields[7].GetUInt32();
            pn->acct = fields[8].GetUInt32();
            pn->m_Group = nullptr;
            pn->subGroup = 0;
            pn->m_guild = 0;
            pn->guildRank = GUILD_RANK_NONE;

            // Raid & heroic Instance IDs
            // Must be done before entering world...
            QueryResult* result2 = CharacterDatabase.Query("SELECT instanceid, mode, mapid FROM instanceids WHERE playerguid = %u", pn->guid);
            if (result2)
            {
                PlayerInstanceMap::iterator itr;
                do
                {
                    uint32 instanceId = result2->Fetch()[0].GetUInt32();
                    uint32 mode = result2->Fetch()[1].GetUInt32();
                    uint32 mapId = result2->Fetch()[2].GetUInt32();
                    if (mode >= InstanceDifficulty::MAX_DIFFICULTY || mapId >= MAX_NUM_MAPS)
                    {
                        continue;
                    }

                    std::lock_guard<std::mutex> lock(pn->savedInstanceIdsLock);
                    itr = pn->savedInstanceIds[mode].find(mapId);
                    if (itr == pn->savedInstanceIds[mode].end())
                    {
                        pn->savedInstanceIds[mode].insert(PlayerInstanceMap::value_type(mapId, instanceId));
                    }
                    else
                    {
                        (*itr).second = instanceId;
                    }

                    ///\todo Instances not loaded yet ~.~
                    //if (!sInstanceMgr.InstanceExists(mapId, pn->m_savedInstanceIds[mapId][mode]))
                    //{
                    //    pn->m_savedInstanceIds[mapId][mode] = 0;
                    //    CharacterDatabase.Execute("DELETE FROM instanceids WHERE mapId = %u AND instanceId = %u AND mode = %u", mapId, instanceId, mode);
                    //}

                } while (result2->NextRow());
                delete result2;
            }

            pn->team = getSideByRace(pn->race);

            std::string lpn = pn->name;
            AscEmu::Util::Strings::toLowerCase(lpn);
            m_playersInfoByName[lpn] = pn;

            //this is startup -> no need in lock -> don't use addplayerinfo
            m_playersinfo[pn->guid] = pn;

        }
        while (result->NextRow());
        delete result;
    }
    sLogger.info("ObjectMgr : %u players loaded.", static_cast<uint32_t>(m_playersinfo.size()));
}

CachedCharacterInfo* ObjectMgr::GetPlayerInfoByName(std::string name)
{
    std::string lpn = std::string(name);
    AscEmu::Util::Strings::toLowerCase(lpn);

    std::lock_guard<std::mutex> guard(playernamelock);

    PlayerNameStringIndexMap::iterator i = m_playersInfoByName.find(lpn);
    if (i != m_playersInfoByName.end())
        return i->second;

    return nullptr;
}

#if VERSION_STRING > TBC
void ObjectMgr::LoadCompletedAchievements()
{
    QueryResult* result = CharacterDatabase.Query("SELECT achievement FROM character_achievement GROUP BY achievement");

    if (!result)
    {
        sLogger.failure("Query failed: SELECT achievement FROM character_achievement");
        return;
    }

    do
    {
        Field* fields = result->Fetch();
        allCompletedAchievements.insert(fields[0].GetUInt32());
    }
    while (result->NextRow());
    delete result;
}
#endif

Corpse* ObjectMgr::LoadCorpse(uint32 guid)
{
    QueryResult* result = CharacterDatabase.Query("SELECT * FROM corpses WHERE guid =%u ", guid);

    if (result == nullptr)
        return nullptr;

    Field* fields = result->Fetch();
    Corpse* pCorpse = new Corpse(HIGHGUID_TYPE_CORPSE, fields[0].GetUInt32());
    pCorpse->SetPosition(fields[1].GetFloat(), fields[2].GetFloat(), fields[3].GetFloat(), fields[4].GetFloat());
    pCorpse->SetZoneId(fields[5].GetUInt32());
    pCorpse->SetMapId(fields[6].GetUInt32());
    pCorpse->setCorpseDataFromDbString(fields[7].GetString());
    if (pCorpse->getDisplayId() == 0)
    {
        delete pCorpse;
        return nullptr;
    }

    pCorpse->SetLoadedFromDB(true);
    pCorpse->SetInstanceID(fields[8].GetUInt32());
    pCorpse->AddToWorld();

    delete result;

    return pCorpse;
}


//////////////////////////////////////////////////////////////////////////////////////////
/// Live corpse retreival.
/// comments: I use the same tricky method to start from the last corpse instead of the first
//////////////////////////////////////////////////////////////////////////////////////////
Corpse* ObjectMgr::GetCorpseByOwner(uint32 ownerguid)
{
    Corpse* rv = nullptr;
    _corpseslock.Acquire();
    for (CorpseMap::const_iterator itr = m_corpses.begin(); itr != m_corpses.end(); ++itr)
    {
        WoWGuid wowGuid;
        wowGuid.Init(itr->second->getOwnerGuid());

        if (wowGuid.getGuidLowPart() == ownerguid)
        {
            rv = itr->second;
            break;
        }
    }
    _corpseslock.Release();

    return rv;
}

void ObjectMgr::DelinkPlayerCorpses(Player* pOwner)
{
    //dupe protection agaisnt crashs
    Corpse* c = this->GetCorpseByOwner(pOwner->getGuidLow());
    if (!c)
        return;
    sEventMgr.AddEvent(c, &Corpse::Delink, EVENT_CORPSE_SPAWN_BONES, 1, 1, EVENT_FLAG_DO_NOT_EXECUTE_IN_WORLD_CONTEXT);
    CorpseAddEventDespawn(c);
}

#if VERSION_STRING > TBC
void ObjectMgr::LoadAchievementRewards()
{
    AchievementRewards.clear();                           // need for reload case

    QueryResult* result = WorldDatabase.Query("SELECT entry, gender, title_A, title_H, item, sender, subject, text FROM achievement_reward");

    if (!result)
    {
        sLogger.info("Loaded 0 achievement rewards. DB table `achievement_reward` is empty.");
        return;
    }

    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();
        uint32 entry = fields[0].GetUInt32();

        if (!sAchievementStore.LookupEntry(entry))
        {
            sLogger.debugFlag(AscEmu::Logging::LF_DB_TABLES, "ObjectMgr : Achievement reward entry %u has wrong achievement, ignore", entry);
            continue;
        }

        AchievementReward reward;
        reward.gender = fields[1].GetUInt32();
        reward.titel_A = fields[2].GetUInt32();
        reward.titel_H = fields[3].GetUInt32();
        reward.itemId = fields[4].GetUInt32();
        reward.sender = fields[5].GetUInt32();
        reward.subject = fields[6].GetString() ? fields[6].GetString() : "";
        reward.text = fields[7].GetString() ? fields[7].GetString() : "";

        if (reward.gender > 2)
            sLogger.debug("ObjectMgr : achievement reward %u has wrong gender %u.", entry, reward.gender);

        bool dup = false;
        AchievementRewardsMapBounds bounds = AchievementRewards.equal_range(entry);
        for (AchievementRewardsMap::const_iterator iter = bounds.first; iter != bounds.second; ++iter)
        {
            if (iter->second.gender == 2 || reward.gender == 2)
            {
                dup = true;
                sLogger.debugFlag(AscEmu::Logging::LF_DB_TABLES, "ObjectMgr : Achievement reward %u must have single GENDER_NONE (%u), ignore duplicate case", 2, entry);
                break;
            }
        }

        if (dup)
            continue;

        // must be title or mail at least
        if (!reward.titel_A && !reward.titel_H && !reward.sender)
        {
            sLogger.debugFlag(AscEmu::Logging::LF_DB_TABLES, "ObjectMgr : achievement_reward %u not have title or item reward data, ignore.", entry);
            continue;
        }

        if (reward.titel_A)
        {
            auto const* char_title_entry = sCharTitlesStore.LookupEntry(reward.titel_A);
            if (!char_title_entry)
            {
                sLogger.debugFlag(AscEmu::Logging::LF_DB_TABLES, "ObjectMgr : achievement_reward %u has invalid title id (%u) in `title_A`, set to 0", entry, reward.titel_A);
                reward.titel_A = 0;
            }
        }

        if (reward.titel_H)
        {
            auto const* char_title_entry = sCharTitlesStore.LookupEntry(reward.titel_H);
            if (!char_title_entry)
            {
                sLogger.debugFlag(AscEmu::Logging::LF_DB_TABLES, "ObjectMgr : achievement_reward %u has invalid title id (%u) in `title_A`, set to 0", entry, reward.titel_H);
                reward.titel_H = 0;
            }
        }

        //check mail data before item for report including wrong item case
        if (reward.sender)
        {
            if (!sMySQLStore.getCreatureProperties(reward.sender))
            {
                sLogger.debugFlag(AscEmu::Logging::LF_DB_TABLES, "ObjectMgr : achievement_reward %u has invalid creature entry %u as sender, mail reward skipped.", entry, reward.sender);
                reward.sender = 0;
            }
        }
        else
        {
            if (reward.itemId)
                sLogger.debugFlag(AscEmu::Logging::LF_DB_TABLES, "ObjectMgr : achievement_reward %u not have sender data but have item reward, item will not rewarded", entry);

            if (!reward.subject.empty())
                sLogger.debugFlag(AscEmu::Logging::LF_DB_TABLES, "ObjectMgr : achievement_reward %u not have sender data but have mail subject.", entry);

            if (!reward.text.empty())
                sLogger.debugFlag(AscEmu::Logging::LF_DB_TABLES, "ObjectMgr : achievement_reward %u not have sender data but have mail text.", entry);
        }

        if (reward.itemId == 0)
        {
            sLogger.debugFlag(AscEmu::Logging::LF_DB_TABLES, "ObjectMgr : achievement_reward %u has invalid item id %u, reward mail will be without item.", entry, reward.itemId);
        }

        AchievementRewards.insert(AchievementRewardsMap::value_type(entry, reward));
        ++count;

    }
    while (result->NextRow());

    delete result;

    sLogger.info("ObjectMgr : Loaded %u achievement rewards", count);
}
#endif

void ObjectMgr::SetHighestGuids()
{
    QueryResult* result = CharacterDatabase.Query("SELECT MAX(guid) FROM characters");
    if (result)
    {
        m_hiPlayerGuid = result->Fetch()[0].GetUInt32();
        delete result;
    }

    result = CharacterDatabase.Query("SELECT MAX(guid) FROM playeritems");
    if (result)
    {
        m_hiItemGuid = result->Fetch()[0].GetUInt32();
        delete result;
    }

    result = CharacterDatabase.Query("SELECT MAX(guid) FROM corpses");
    if (result)
    {
        m_hiCorpseGuid = result->Fetch()[0].GetUInt32();
        delete result;
    }

    result = WorldDatabase.Query("SELECT MAX(id) FROM creature_spawns WHERE min_build <= %u AND max_build >= %u AND event_entry = 0;", VERSION_STRING, VERSION_STRING);
    if (result)
    {
        m_hiCreatureSpawnId = result->Fetch()[0].GetUInt32();
        delete result;
    }

    result = WorldDatabase.Query("SELECT MAX(id) FROM gameobject_spawns WHERE min_build <= %u AND max_build >= %u AND event_entry = 0;", VERSION_STRING, VERSION_STRING);
    if (result)
    {
        m_hiGameObjectSpawnId = result->Fetch()[0].GetUInt32();
        delete result;
    }

    result = CharacterDatabase.Query("SELECT MAX(group_id) FROM `groups`");
    if (result)
    {
        m_hiGroupId = result->Fetch()[0].GetUInt32();
        delete result;
    }

    result = CharacterDatabase.Query("SELECT MAX(charterid) FROM charters");
    if (result)
    {
        m_hiCharterId = result->Fetch()[0].GetUInt32();
        delete result;
    }

    result = CharacterDatabase.Query("SELECT MAX(guildid) FROM guilds");
    if (result)
    {
        m_hiGuildId = result->Fetch()[0].GetUInt32();
        delete result;
    }

    result = CharacterDatabase.Query("SELECT MAX(UID) FROM playerbugreports");
    if (result != nullptr)
    {
        m_reportID = uint32(result->Fetch()[0].GetUInt64() + 1);
        delete result;
    }

    result = CharacterDatabase.Query("SELECT MAX(message_id) FROM mailbox");
    if (result)
    {
        m_mailid = uint32(result->Fetch()[0].GetUInt64() + 1);
        delete result;
    }

    result = CharacterDatabase.Query("SELECT MAX(setGUID) FROM equipmentsets");
    if (result != nullptr)
    {
        m_setGUID = uint32(result->Fetch()[0].GetUInt32() + 1);
        delete result;
    }

#if VERSION_STRING > WotLK
    result = CharacterDatabase.Query("SELECT MAX(itemId) FROM character_void_storage");
    if (result  != nullptr)
    {
        m_voidItemId = uint64_t(result->Fetch()[0].GetUInt32() + 1);
    }
#endif

    sLogger.info("ObjectMgr : HighGuid(CORPSE) = %lu", m_hiCorpseGuid.load());
    sLogger.info("ObjectMgr : HighGuid(PLAYER) = %lu", m_hiPlayerGuid.load());
    sLogger.info("ObjectMgr : HighGuid(GAMEOBJ) = %lu", m_hiGameObjectSpawnId.load());
    sLogger.info("ObjectMgr : HighGuid(UNIT) = %lu", m_hiCreatureSpawnId.load());
    sLogger.info("ObjectMgr : HighGuid(ITEM) = %lu", m_hiItemGuid.load());
    sLogger.info("ObjectMgr : HighGuid(CONTAINER) = %lu", m_hiItemGuid.load());
    sLogger.info("ObjectMgr : HighGuid(GROUP) = %lu", m_hiGroupId.load());
    sLogger.info("ObjectMgr : HighGuid(CHARTER) = %lu", m_hiCharterId.load());
    sLogger.info("ObjectMgr : HighGuid(GUILD) = %lu", m_hiGuildId.load());
    sLogger.info("ObjectMgr : HighGuid(BUGREPORT) = %u", uint32(m_reportID.load() - 1));
    sLogger.info("ObjectMgr : HighGuid(MAIL) = %u", uint32(m_mailid.load()));
    sLogger.info("ObjectMgr : HighGuid(EQUIPMENTSET) = %u", uint32(m_setGUID.load() - 1));
}

uint32 ObjectMgr::GenerateReportID()
{
    return ++m_reportID;
}

uint32 ObjectMgr::GenerateEquipmentSetID()
{
    return ++m_setGUID;
}

uint32 ObjectMgr::GenerateMailID()
{
    return ++m_mailid;
}

uint32 ObjectMgr::GenerateLowGuid(uint32 guidhigh)
{
    uint32 ret;

    switch (guidhigh)
    {
    case HIGHGUID_TYPE_PLAYER:
        ret = ++m_hiPlayerGuid;
        break;
    case HIGHGUID_TYPE_ITEM:
    case HIGHGUID_TYPE_CONTAINER:
        ret = ++m_hiItemGuid;
        break;
    default:
        sLogger.failure("ObjectMgr::GenerateLowGuid tried to generate low guid gor non player/item, return 0!");
        ret = 0;
        break;
    }

    return ret;
}

Player* ObjectMgr::GetPlayer(const char* name, bool caseSensitive)
{
    std::lock_guard<std::mutex> guard(_playerslock);

    if (!caseSensitive)
    {
        std::string strName = name;
        AscEmu::Util::Strings::toLowerCase(strName);
        for (PlayerStorageMap::const_iterator itr = _players.begin(); itr != _players.end(); ++itr)
        {
            if (!stricmp(itr->second->getName().c_str(), strName.c_str()))
            {
                return itr->second;
            }
        }
    }
    else
    {
        for (PlayerStorageMap::const_iterator itr = _players.begin(); itr != _players.end(); ++itr)
        {
            if (!strcmp(itr->second->getName().c_str(), name))
            {
                return itr->second;
            }
        }
    }

    return nullptr;
}

Player* ObjectMgr::GetPlayer(uint32 guid)
{
    std::lock_guard<std::mutex> guard(_playerslock);

    PlayerStorageMap::const_iterator itr = _players.find(guid);
    return (itr != _players.end()) ? itr->second : nullptr;
}

void ObjectMgr::LoadVendors()
{
    QueryResult* result = WorldDatabase.Query("SELECT * FROM vendors");
    if (result != nullptr)
    {
        std::unordered_map<uint32, std::vector<CreatureItem>*>::const_iterator itr;
        std::vector<CreatureItem> *items;

        if (result->GetFieldCount() < 6)
        {
            sLogger.failure("Invalid format in vendors (%u/6) columns, not enough data to proceed.", result->GetFieldCount());
            delete result;
            return;
        }
        else if (result->GetFieldCount() > 6)
        {
            sLogger.failure("Invalid format in vendors (%u/6) columns, loading anyway because we have enough data", result->GetFieldCount());
        }

#if VERSION_STRING < Cata
        DBC::Structures::ItemExtendedCostEntry const* item_extended_cost = nullptr;
#else
        DB2::Structures::ItemExtendedCostEntry const* item_extended_cost = nullptr;
#endif
        do
        {
            Field* fields = result->Fetch();

            itr = mVendors.find(fields[0].GetUInt32());

            if (itr == mVendors.end())
            {
                items = new std::vector < CreatureItem > ;
                mVendors[fields[0].GetUInt32()] = items;
            }
            else
            {
                items = itr->second;
            }

            CreatureItem itm;
            itm.itemid = fields[1].GetUInt32();
            itm.amount = fields[2].GetUInt32();
            itm.available_amount = fields[3].GetUInt32();
            itm.max_amount = fields[3].GetUInt32();
            itm.incrtime = fields[4].GetUInt32();
            if (fields[5].GetUInt32() > 0)
            {
                item_extended_cost = sItemExtendedCostStore.LookupEntry(fields[5].GetUInt32());
                if (item_extended_cost == nullptr)
                    sLogger.debug("LoadVendors : Extendedcost for item %u references nonexistent EC %u", fields[1].GetUInt32(), fields[5].GetUInt32());
            }
            else
                item_extended_cost = nullptr;

            itm.extended_cost = item_extended_cost;
            items->push_back(itm);
        }
        while (result->NextRow());

        delete result;
    }
    sLogger.info("ObjectMgr : %u vendors loaded.", static_cast<uint32_t>(mVendors.size()));
}

void ObjectMgr::ReloadVendors()
{
    mVendors.clear();
    LoadVendors();
}

std::vector<CreatureItem>* ObjectMgr::GetVendorList(uint32 entry)
{
    return mVendors[entry];
}

Item* ObjectMgr::CreateItem(uint32 entry, Player* owner)
{
    ItemProperties const* proto = sMySQLStore.getItemProperties(entry);
    if (proto ==nullptr)
        return nullptr;

    if (proto->InventoryType == INVTYPE_BAG)
    {
        Container* pContainer = new Container(HIGHGUID_TYPE_CONTAINER, GenerateLowGuid(HIGHGUID_TYPE_CONTAINER));
        pContainer->Create(entry, owner);
        pContainer->setStackCount(1);
        return pContainer;
    }
    else
    {
        Item* pItem = new Item;
        pItem->init(HIGHGUID_TYPE_ITEM, GenerateLowGuid(HIGHGUID_TYPE_ITEM));
        pItem->create(entry, owner);
        pItem->setStackCount(1);

#if VERSION_STRING > TBC
        if (owner != nullptr)
        {
            uint32* played = owner->GetPlayedtime();
            pItem->setCreatePlayedTime(played[1]);
        }
#endif

        return pItem;
    }
}

Item* ObjectMgr::LoadItem(uint32 lowguid)
{
    QueryResult* result = CharacterDatabase.Query("SELECT * FROM playeritems WHERE guid = %u", lowguid);
    Item* pReturn = nullptr;

    if (result)
    {
        ItemProperties const* pProto = sMySQLStore.getItemProperties(result->Fetch()[2].GetUInt32());
        if (!pProto)
            return nullptr;

        if (pProto->InventoryType == INVTYPE_BAG)
        {
            Container* pContainer = new Container(HIGHGUID_TYPE_CONTAINER, lowguid);
            pContainer->LoadFromDB(result->Fetch());
            pReturn = pContainer;
        }
        else
        {
            Item* pItem = new Item;
            pItem->init(HIGHGUID_TYPE_ITEM, lowguid);
            pItem->LoadFromDB(result->Fetch(), nullptr, false);
            pReturn = pItem;
        }
        delete result;
    }

    return pReturn;
}

void ObjectMgr::LoadCorpses(MapMgr* mgr)
{
    QueryResult* result = CharacterDatabase.Query("SELECT * FROM corpses WHERE instanceid = %u", mgr->GetInstanceID());

    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            Corpse* pCorpse = new Corpse(HIGHGUID_TYPE_CORPSE, fields[0].GetUInt32());
            pCorpse->SetPosition(fields[1].GetFloat(), fields[2].GetFloat(), fields[3].GetFloat(), fields[4].GetFloat());
            pCorpse->SetZoneId(fields[5].GetUInt32());
            pCorpse->SetMapId(fields[6].GetUInt32());
            pCorpse->SetInstanceID(fields[7].GetUInt32());
            pCorpse->setCorpseDataFromDbString(fields[8].GetString());
            if (pCorpse->getDisplayId() == 0)
            {
                delete pCorpse;
                continue;
            }

            pCorpse->PushToWorld(mgr);
        }
        while (result->NextRow());

        delete result;
    }
}

#if VERSION_STRING > TBC
AchievementCriteriaEntryList const & ObjectMgr::GetAchievementCriteriaByType(AchievementCriteriaTypes type)
{
    return m_AchievementCriteriasByType[type];
}

void ObjectMgr::LoadAchievementCriteriaList()
{
    for (uint32 rowId = 0; rowId < sAchievementCriteriaStore.GetNumRows(); ++rowId)
    {
        auto criteria = sAchievementCriteriaStore.LookupEntry(rowId);
        if (!criteria)
            continue;

#if VERSION_STRING > WotLK
        auto achievement = sAchievementStore.LookupEntry(criteria->referredAchievement);
        if (achievement && achievement->flags & ACHIEVEMENT_FLAG_GUILD)
            m_GuildAchievementCriteriasByType[criteria->requiredType].push_back(criteria);
        else
#endif
            m_AchievementCriteriasByType[criteria->requiredType].push_back(criteria);
    }
}
#endif

void ObjectMgr::CorpseAddEventDespawn(Corpse* pCorpse)
{
    if (!pCorpse->IsInWorld())
        delete pCorpse;
    else
        sEventMgr.AddEvent(pCorpse->GetMapMgr(), &MapMgr::EventCorpseDespawn, pCorpse->getGuid(), EVENT_CORPSE_DESPAWN, 600000, 1, EVENT_FLAG_DO_NOT_EXECUTE_IN_WORLD_CONTEXT);
}

void ObjectMgr::CorpseCollectorUnload()
{
    CorpseMap::const_iterator itr;
    _corpseslock.Acquire();
    for (itr = m_corpses.begin(); itr != m_corpses.end(); ++itr)
    {
        Corpse* c = itr->second;
        if (c->IsInWorld())
            c->RemoveFromWorld(false);
        delete c;
    }
    m_corpses.clear();
    _corpseslock.Release();
}

//move to spellmgr or mysqldatastore todo danko
void ObjectMgr::LoadSkillLineAbilityMap()
{
    auto startTime = Util::TimeNow();

    mSkillLineAbilityMap.clear();

    uint32_t count = 0;
    for (uint32_t i = 0; i < sSkillLineAbilityStore.GetNumRows(); ++i)
    {
        DBC::Structures::SkillLineAbilityEntry const* SkillInfo = sSkillLineAbilityStore.LookupEntry(i);
        if (!SkillInfo)
            continue;

        mSkillLineAbilityMap.insert(SkillLineAbilityMap::value_type(SkillInfo->Id, SkillInfo));
        ++count;
    }

    sLogger.info("ObjectMgr : Loaded %u SkillLineAbility MultiMap Data in %u ms", count, static_cast<uint32_t>(Util::GetTimeDifferenceToNow(startTime)));
}

SkillLineAbilityMapBounds ObjectMgr::GetSkillLineAbilityMapBounds(uint32_t spell_id) const
{
    return mSkillLineAbilityMap.equal_range(spell_id);
}

void ObjectMgr::LoadSpellRequired()
{
    auto startTime = Util::TimeNow();

    mSpellsReqSpell.clear();    // need for reload case
    mSpellReq.clear();          // need for reload case

    //                                                   0         1
    QueryResult* result = WorldDatabase.Query("SELECT spell_id, req_spell FROM spell_required");

    if (!result)
    {
        sLogger.debug("ObjectMgr : Loaded 0 spell required records. DB table `spell_required` is empty.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        uint32_t spell_id = fields[0].GetUInt32();
        uint32_t spell_req = fields[1].GetUInt32();

        // check if chain is made with valid first spell
        DBC::Structures::SpellEntry const* spell = sSpellStore.LookupEntry(spell_id);
        if (!spell)
        {
            sLogger.debug("ObjectMgr : spell_id %u in `spell_required` table is not found in dbcs, skipped", spell_id);
            continue;
        }

        DBC::Structures::SpellEntry const* req_spell = sSpellStore.LookupEntry(spell_req);
        if (!req_spell)
        {
            sLogger.debug("ObjectMgr : req_spell %u in `spell_required` table is not found in dbcs, skipped", spell_req);
            continue;
        }

        if (IsSpellRequiringSpell(spell_id, spell_req))
        {
            sLogger.debug("ObjectMgr : duplicated entry of req_spell %u and spell_id %u in `spell_required`, skipped", spell_req, spell_id);
            continue;
        }

        mSpellReq.insert(std::pair<uint32_t, uint32_t>(spell_id, spell_req));
        mSpellsReqSpell.insert(std::pair<uint32_t, uint32_t>(spell_req, spell_id));
        ++count;

    } while (result->NextRow());

    sLogger.info("ObjectMgr: Loaded %u spell required records in %u ms", count, static_cast<uint32_t>(Util::GetTimeDifferenceToNow(startTime)));
}


SpellRequiredMapBounds ObjectMgr::GetSpellsRequiredForSpellBounds(uint32_t spell_id) const
{
    return mSpellReq.equal_range(spell_id);
}

SpellsRequiringSpellMapBounds ObjectMgr::GetSpellsRequiringSpellBounds(uint32_t spell_id) const
{
    return mSpellsReqSpell.equal_range(spell_id);
}

bool ObjectMgr::IsSpellRequiringSpell(uint32_t spellid, uint32_t req_spellid) const
{
    SpellsRequiringSpellMapBounds spellsRequiringSpell = GetSpellsRequiringSpellBounds(req_spellid);
    for (SpellsRequiringSpellMap::const_iterator itr = spellsRequiringSpell.first; itr != spellsRequiringSpell.second; ++itr)
    {
        if (itr->second == spellid)
            return true;
    }
    return false;
}

const SpellsRequiringSpellMap ObjectMgr::GetSpellsRequiringSpell()
{
    return this->mSpellsReqSpell;
}

uint32_t ObjectMgr::GetSpellRequired(uint32_t spell_id) const
{
    SpellRequiredMap::const_iterator itr = mSpellReq.find(spell_id);

    if (itr == mSpellReq.end())
        return 0;

    return itr->second;
}

//MIT
void ObjectMgr::generateDatabaseGossipMenu(Object* object, uint32_t gossipMenuId, Player* player, uint32_t forcedTextId /*= 0*/)
{
    uint32_t textId = 2;

    if (forcedTextId == 0)
    {
        auto gossipMenuTextStore = sMySQLStore.getGossipMenuInitTextId();
        for (auto &initItr : *gossipMenuTextStore)
        {
            if (initItr.first == gossipMenuId)
            {
                textId = initItr.second.textId;
                break;
            }
        }
    }
    else
    {
        textId = forcedTextId;
    }

    GossipMenu menu(object->getGuid(), textId, player->GetSession()->language, gossipMenuId);

    sQuestMgr.FillQuestMenu(dynamic_cast<Creature*>(object), player, menu);

    typedef MySQLDataStore::GossipMenuItemsContainer::iterator GossipMenuItemsIterator;
    std::pair<GossipMenuItemsIterator, GossipMenuItemsIterator> gossipEqualRange = sMySQLStore._gossipMenuItemsStores.equal_range(gossipMenuId);
    for (GossipMenuItemsIterator itr = gossipEqualRange.first; itr != gossipEqualRange.second; ++itr)
    {
        // check requirements
        // 0 = none
        // 1 = has(active)Quest
        // 2 = has(finished)Quest
        // 3 = canGainXP
        // 4 = canNotGainXP

        if (itr->first == gossipMenuId)
        {
            if (itr->second.requirementType == 1 && !player->hasQuestInQuestLog(itr->second.requirementData))
                continue;

            if (itr->second.requirementType == 3)
            {
                if (player->CanGainXp())
                    menu.addItem(itr->second.icon, itr->second.menuOptionText, itr->second.itemOrder, "", itr->second.onChooseData, player->GetSession()->LocalizedGossipOption(itr->second.onChooseData2));
                
                continue;
            }

            if (itr->second.requirementType == 4)
            {
                if (!player->CanGainXp())
                    menu.addItem(itr->second.icon, itr->second.menuOptionText, itr->second.itemOrder, "", itr->second.onChooseData, player->GetSession()->LocalizedGossipOption(itr->second.onChooseData2));
                
                continue;
            }

            menu.addItem(itr->second.icon, itr->second.menuOptionText, itr->second.itemOrder);
        }
    }

    menu.sendGossipPacket(player);
}

//MIT
void ObjectMgr::generateDatabaseGossipOptionAndSubMenu(Object* object, Player* player, uint32_t gossipItemId, uint32_t gossipMenuId)
{
    sLogger.debug("GossipId: %u  gossipItemId: %u", gossipMenuId, gossipItemId);

    // bool openSubMenu = true;

    typedef MySQLDataStore::GossipMenuItemsContainer::iterator GossipMenuItemsIterator;
    std::pair<GossipMenuItemsIterator, GossipMenuItemsIterator> gossipEqualRange = sMySQLStore._gossipMenuItemsStores.equal_range(gossipMenuId);
    for (GossipMenuItemsIterator itr = gossipEqualRange.first; itr != gossipEqualRange.second; ++itr)
    {
        if (itr->second.itemOrder == gossipItemId)
        {
            // onChooseAction
            // 0 = None
            // 1 = sendPoiById (on_choose_data = poiId)
            // 2 = castSpell (on_choose_data = spellId)
            // 3 = sendTaxi (on_choose_data = taxiId, on_choose_data2 = modelId)
            // 4 = required standing (on_choose_data = factionId, on_choose_data2 = standing, on_choose_data3 = broadcastTextId)
            // 5 = close window
            // 6 = toggleXPGain

            // onChooseData
            // depending on Action...
            switch (itr->second.onChooseAction)
            {
                case 1:
                {
                    generateDatabaseGossipMenu(object, itr->second.nextGossipMenu, player, itr->second.nextGossipMenuText);

                    if (itr->second.onChooseData != 0)
                        player->sendPoiById(itr->second.onChooseData);

                } break;
                case 2:
                {
                    if (itr->second.onChooseData != 0)
                    {
                        player->castSpell(player, sSpellMgr.getSpellInfo(itr->second.onChooseData), true);
                        GossipMenu::senGossipComplete(player);
                    }

                } break;
                case 3:
                {
                    if (itr->second.onChooseData != 0)
                    {
                        player->TaxiStart(sTaxiMgr.GetTaxiPath(itr->second.onChooseData), itr->second.onChooseData2, 0);
                        GossipMenu::senGossipComplete(player);
                    }

                } break;
                case 4:
                {
                    if (itr->second.onChooseData != 0)
                    {
                        if (player->GetStanding(itr->second.onChooseData) >= static_cast<int32_t>(itr->second.onChooseData2))
                            player->castSpell(player, sSpellMgr.getSpellInfo(itr->second.onChooseData3), true);
                        else
                            player->BroadcastMessage(player->GetSession()->LocalizedWorldSrv(itr->second.onChooseData4));
                        
                        GossipMenu::senGossipComplete(player);
                    }

                } break;
                case 5:
                {
                    GossipMenu::senGossipComplete(player);

                } break;
                case 6:
                {
                    if (player->hasEnoughCoinage(itr->second.onChooseData))
                    {
                        player->modCoinage(-static_cast<int32_t>(itr->second.onChooseData));
                        player->ToggleXpGain();
                        GossipMenu::senGossipComplete(player);
                    }
                } break;
                default: // action 0
                {
                    generateDatabaseGossipMenu(object, itr->second.nextGossipMenu, player, itr->second.nextGossipMenuText);
                } break;
            }
        }
    }
}

//MIT
void ObjectMgr::loadTrainers()
{
#if VERSION_STRING > TBC    //todo: tbc
    auto* const result = WorldDatabase.Query("SELECT * FROM trainer_defs");
    LoadDisabledSpells();

    if (result == nullptr)
        return;

    do
    {
        auto* const fields = result->Fetch();
        const auto entry = fields[0].GetUInt32();

        Trainer* tr = new Trainer;
        tr->RequiredSkill = fields[1].GetUInt32();
        tr->RequiredSkillLine = fields[2].GetUInt32();
        tr->RequiredClass = fields[3].GetUInt32();
        tr->RequiredRace = fields[4].GetUInt32();
        tr->RequiredRepFaction = fields[5].GetUInt32();
        tr->RequiredRepValue = fields[6].GetUInt32();
        tr->TrainerType = fields[7].GetUInt32();
        tr->Can_Train_Gossip_TextId = fields[9].GetUInt32();
        tr->Cannot_Train_GossipTextId = fields[10].GetUInt32();

        if (!tr->Can_Train_Gossip_TextId)
            tr->Can_Train_Gossip_TextId = 1;
        if (!tr->Cannot_Train_GossipTextId)
            tr->Cannot_Train_GossipTextId = 1;

        const char* temp = fields[8].GetString();
        size_t len = strlen(temp);
        if (len)
        {
            tr->UIMessage = new char[len + 1];
            strcpy(tr->UIMessage, temp);
            tr->UIMessage[len] = 0;
        }
        else
        {
            tr->UIMessage = new char[strlen(NormalTalkMessage) + 1];
            strcpy(tr->UIMessage, NormalTalkMessage);
            tr->UIMessage[strlen(NormalTalkMessage)] = 0;
        }

        // Now load the spells
        auto* const result2 = WorldDatabase.Query("SELECT * FROM trainer_spells where entry='%u'", entry);
        if (result2 == nullptr)
        {
            sLogger.debug("LoadTrainers : Trainer with no spells, entry %u.", entry);
            if (tr->UIMessage != NormalTalkMessage)
                delete[] tr->UIMessage;

            delete tr;
            continue;
        }

        if (result2->GetFieldCount() != 9)
        {
            sLogger.failure("trainer_spells table format is invalid. Please update your database.");
            delete tr;
            delete result;
            delete result2;
            return;
        }

        do
        {
            auto* const fields2 = result2->Fetch();
            TrainerSpell ts;
            auto abrt = false;
            auto castSpellID = fields2[1].GetUInt32();
            auto learnSpellID = fields2[2].GetUInt32();

            if (castSpellID != 0)
            {
                ts.castSpell = sSpellMgr.getSpellInfo(castSpellID);
                if (ts.castSpell != nullptr)
                {
                    // Check that the castable spell has learn spell effect
                    for (uint8_t i = 0; i < MAX_SPELL_EFFECTS; ++i)
                    {
                        if (ts.castSpell->getEffect(i) == SPELL_EFFECT_LEARN_SPELL)
                        {
                            ts.castRealSpell = sSpellMgr.getSpellInfo(ts.castSpell->getEffectTriggerSpell(i));
                            if (ts.castRealSpell == nullptr)
                            {
                                sLogger.failure("LoadTrainers : Trainer %u contains cast spell %u that is non-teaching", entry, castSpellID);
                                abrt = true;
                            }

                            break;
                        }
                    }
                }

                if (abrt)
                    continue;
            }

            if (learnSpellID != 0)
                ts.learnSpell = sSpellMgr.getSpellInfo(learnSpellID);

            if (ts.castSpell == nullptr && ts.learnSpell == nullptr)
            {
                // Trainer spell entry has invalid spells, skip this entry
                continue;
            }

            if (ts.castSpell != nullptr && ts.castRealSpell == nullptr)
                continue;

            ts.cost = fields2[3].GetUInt32();
            ts.requiredSpell[0] = fields2[4].GetUInt32();
            // todo: add to database
            ts.requiredSpell[1] = 0;
            // todo: add to database
            ts.requiredSpell[2] = 0;
            ts.requiredSkillLine = fields2[5].GetUInt32();
            ts.requiredSkillLineValue = fields2[6].GetUInt32();
            ts.requiredLevel = fields2[7].GetUInt32();
            ts.deleteSpell = fields2[8].GetUInt32();

            // Check if spell teaches a primary profession skill
            if (ts.requiredSkillLine == 0 && ts.castRealSpell != nullptr)
                ts.isPrimaryProfession = ts.castRealSpell->isPrimaryProfession();

            tr->Spells.push_back(ts);
        } while (result2->NextRow());
        delete result2;

        tr->SpellCount = static_cast<uint32_t>(tr->Spells.size());

        // and now we insert it to our lookup table
        if (tr->SpellCount == 0)
        {
            if (tr->UIMessage != NormalTalkMessage)
                delete[] tr->UIMessage;
            delete tr;
            continue;
        }

        mTrainers.insert(TrainerMap::value_type(entry, tr));
    }
    while (result->NextRow());

    delete result;
    sLogger.info("ObjectMgr : %u trainers loaded.", static_cast<uint32_t>(mTrainers.size()));
#endif
}

Trainer* ObjectMgr::GetTrainer(uint32 Entry)
{
    TrainerMap::iterator iter = mTrainers.find(Entry);
    if (iter == mTrainers.end())
        return nullptr;

    return iter->second;
}

void ObjectMgr::GenerateLevelUpInfo()
{
    struct MissingLevelData
    {
        uint32_t _level;
        uint8_t _race;
        uint8_t _class;
    };

    std::vector<MissingLevelData> _missingHealthLevelData;
    std::vector<MissingLevelData> _missingStatLevelData;

    // Copy existing level stats

    uint32_t levelstat_counter = 0;
    uint32_t class_levelstat_counter = 0;
    for (uint8 Class = WARRIOR; Class < MAX_PLAYER_CLASSES; ++Class)
    {
        for (uint8 Race = RACE_HUMAN; Race < DBC_NUM_RACES; ++Race)
        {
            if (!isClassRaceCombinationPossible(Class, Race))
            {
                if (auto* playerLevelstats = sMySQLStore.getPlayerLevelstats(1, Race, Class))
                {
                    sLogger.info("ObjectMgr : Invalid class/race combination! %u class and %u race.", uint32_t(Class), uint32_t(Race));
                    sLogger.info("ObjectMgr : But class/race values for level 1 in db!");
                }
                continue;
            }

            LevelMap* levelMap = new LevelMap;

            for (uint32_t level = 1; level <= worldConfig.player.playerLevelCap; ++level)
            {
                LevelInfo* levelInfo = new LevelInfo;

                if (auto* playerClassLevelstats = sMySQLStore.getPlayerClassLevelStats(level, Class))
                {
                    levelInfo->HP = playerClassLevelstats->health;
                    levelInfo->Mana = playerClassLevelstats->mana;
                    ++class_levelstat_counter;
                }
                else  //calculate missing stats based on last level
                {
                    levelInfo->HP = 0;
                    levelInfo->Mana = 0;

                    _missingHealthLevelData.push_back({ level, Race, Class });
                }

                if (auto* playerLevelstats = sMySQLStore.getPlayerLevelstats(level, Race, Class))
                {
                    levelInfo->Stat[0] = playerLevelstats->strength;
                    levelInfo->Stat[1] = playerLevelstats->agility;
                    levelInfo->Stat[2] = playerLevelstats->stamina;
                    levelInfo->Stat[3] = playerLevelstats->intellect;
                    levelInfo->Stat[4] = playerLevelstats->spirit;
                    ++levelstat_counter;
                }
                else //calculate missing stats based on last level
                {
                    for (uint8_t id = 0; id < 5; ++id)
                        levelInfo->Stat[id] = 0;

                    _missingStatLevelData.push_back({ level, Race, Class });
                }

                // Insert into map
                levelMap->insert(LevelMap::value_type(level, levelInfo));
            }

            // Insert back into the main map.
            mLevelInfo.insert(LevelInfoMap::value_type(std::make_pair(Race, Class), levelMap));
        }
    }

    sLogger.info("ObjectMgr : %u levelstats and %u classlevelstats applied from db.", levelstat_counter, class_levelstat_counter);

    // generate missing data
    uint32_t hp_counter = 0;
    for (auto missingHP : _missingHealthLevelData)
    {
        uint32 TotalHealthGain = 0;
        uint32 TotalManaGain = 0;

        // use legacy gaining
        switch (missingHP._class)
        {
        case WARRIOR:
            if (missingHP._level < 13) TotalHealthGain += 19;
            else if (missingHP._level < 36) TotalHealthGain += missingHP._level + 6;
            else if (missingHP._level > 60) TotalHealthGain += missingHP._level + 206;
            else TotalHealthGain += 2 * missingHP._level - 30;
            break;
        case HUNTER:
            if (missingHP._level < 13) TotalHealthGain += 17;
            else if (missingHP._level > 60) TotalHealthGain += missingHP._level + 161;
            else TotalHealthGain += missingHP._level + 4;

            if (missingHP._level < 11) TotalManaGain += 29;
            else if (missingHP._level < 27) TotalManaGain += missingHP._level + 18;
            else if (missingHP._level > 60) TotalManaGain += missingHP._level + 150;
            else TotalManaGain += 45;
            break;
        case ROGUE:
            if (missingHP._level < 15) TotalHealthGain += 17;
            else if (missingHP._level > 60) TotalHealthGain += missingHP._level + 191;
            else TotalHealthGain += missingHP._level + 2;
            break;
        case DRUID:
            if (missingHP._level < 17) TotalHealthGain += 17;
            else if (missingHP._level > 60) TotalHealthGain += missingHP._level + 176;
            else TotalHealthGain += missingHP._level;

            if (missingHP._level < 26) TotalManaGain += missingHP._level + 20;
            else if (missingHP._level > 60) TotalManaGain += missingHP._level + 150;
            else TotalManaGain += 45;
            break;
        case MAGE:
            if (missingHP._level < 23) TotalHealthGain += 15;
            else if (missingHP._level > 60) TotalHealthGain += missingHP._level + 190;
            else TotalHealthGain += missingHP._level - 8;

            if (missingHP._level < 28) TotalManaGain += missingHP._level + 23;
            else if (missingHP._level > 60) TotalManaGain += missingHP._level + 115;
            else TotalManaGain += 51;
            break;
        case SHAMAN:
            if (missingHP._level < 16) TotalHealthGain += 17;
            else if (missingHP._level > 60) TotalHealthGain += missingHP._level + 157;
            else TotalHealthGain += missingHP._level + 1;

            if (missingHP._level < 22) TotalManaGain += missingHP._level + 19;
            else if (missingHP._level > 60) TotalManaGain += missingHP._level + 175;
            else TotalManaGain += 49;
            break;
        case WARLOCK:
            if (missingHP._level < 17) TotalHealthGain += 17;
            else if (missingHP._level > 60) TotalHealthGain += missingHP._level + 192;
            else TotalHealthGain += missingHP._level - 2;

            if (missingHP._level < 30) TotalManaGain += missingHP._level + 21;
            else if (missingHP._level > 60) TotalManaGain += missingHP._level + 121;
            else TotalManaGain += 51;
            break;
        case PALADIN:
            if (missingHP._level < 14) TotalHealthGain += 18;
            else if (missingHP._level > 60) TotalHealthGain += missingHP._level + 167;
            else TotalHealthGain += missingHP._level + 4;

            if (missingHP._level < 30) TotalManaGain += missingHP._level + 17;
            else if (missingHP._level > 60) TotalManaGain += missingHP._level + 131;
            else TotalManaGain += 42;
            break;
        case PRIEST:
            if (missingHP._level < 21) TotalHealthGain += 15;
            else if (missingHP._level > 60) TotalHealthGain += missingHP._level + 157;
            else TotalHealthGain += missingHP._level - 6;

            if (missingHP._level < 22) TotalManaGain += missingHP._level + 22;
            else if (missingHP._level < 32) TotalManaGain += missingHP._level + 37;
            else if (missingHP._level > 60) TotalManaGain += missingHP._level + 207;
            else TotalManaGain += 54;
            break;
        case DEATHKNIGHT:
            TotalHealthGain += 92;
            break;
        default:
            TotalHealthGain += 15;
            TotalManaGain += 45;
            break;
        }

        if (auto level_info = sObjectMgr.GetLevelInfo(missingHP._race, missingHP._class, missingHP._level))
        {
            level_info->HP = level_info->HP + TotalHealthGain;
            level_info->Mana = level_info->Mana + TotalManaGain;
            ++hp_counter;
        }
    }

    uint32_t stat_counter = 0;
    for (auto missingStat : _missingStatLevelData)
    {
        if (auto level_info = sObjectMgr.GetLevelInfo(missingStat._race, missingStat._class, missingStat._level))
        {
            uint32 val;
            for (uint8_t id = 0; id < 5; ++id)
            {
                val = GainStat(static_cast<uint16>(missingStat._level), missingStat._class, id);
                level_info->Stat[id] = level_info->Stat[id] + val;
            }

            ++stat_counter;
        }
    }

    sLogger.info("ObjectMgr : %u level up information generated.", (stat_counter + hp_counter));

}


LevelInfo* ObjectMgr::GetLevelInfo(uint32 Race, uint32 Class, uint32 Level)
{
    // Iterate levelinfo map until we find the right class+race.
    LevelInfoMap::iterator itr = mLevelInfo.begin();
    for (; itr != mLevelInfo.end(); ++itr)
    {
        if (itr->first.first == Race && itr->first.second == Class)
        {
            // We got a match.
            // Let's check that our level is valid first.
            if (Level > worldConfig.player.playerLevelCap)
                Level = worldConfig.player.playerLevelCap;

            // Pull the level information from the second map.
            LevelMap::iterator it2 = itr->second->find(Level);
            if (it2 == itr->second->end())
            {
                sLogger.info("GetLevelInfo : No level information found for level %u!", Level);
                return nullptr;
            }

            return it2->second;
        }
    }

    return nullptr;
}

void ObjectMgr::LoadPetSpellCooldowns()
{
    for (uint32 i = 0; i < sCreatureSpellDataStore.GetNumRows(); ++i)
    {
        auto creture_spell_data = sCreatureSpellDataStore.LookupEntry(i);

        for (uint8 j = 0; j < 3; ++j)
        {
            if (creture_spell_data == nullptr)
                continue;

            uint32 SpellId = creture_spell_data->Spells[j];
            uint32 Cooldown = creture_spell_data->Cooldowns[j] * 10;

            if (SpellId != 0)
            {
                PetSpellCooldownMap::iterator itr = mPetSpellCooldowns.find(SpellId);
                if (itr == mPetSpellCooldowns.end())
                {
                    if (Cooldown)
                        mPetSpellCooldowns.insert(std::make_pair(SpellId, Cooldown));
                }
            }
        }
    }
}

uint32 ObjectMgr::GetPetSpellCooldown(uint32 SpellId)
{
    PetSpellCooldownMap::iterator itr = mPetSpellCooldowns.find(SpellId);
    if (itr != mPetSpellCooldowns.end())
        return itr->second;

    SpellInfo const* sp = sSpellMgr.getSpellInfo(SpellId);
    if (sp->getRecoveryTime() > sp->getCategoryRecoveryTime())
        return sp->getRecoveryTime();
    else
        return sp->getCategoryRecoveryTime();
}

void ObjectMgr::SetVendorList(uint32 Entry, std::vector<CreatureItem>* list_)
{
    mVendors[Entry] = list_;
}

void ObjectMgr::LoadCreatureTimedEmotes()
{
    QueryResult* result = WorldDatabase.Query("SELECT * FROM creature_timed_emotes order by rowid asc");
    if (!result)return;

    do
    {
        Field* fields = result->Fetch();
        spawn_timed_emotes* te = new spawn_timed_emotes;
        te->type = fields[2].GetUInt8();
        te->value = fields[3].GetUInt32();
        char* str = (char*)fields[4].GetString();
        if (str)
        {
            uint32 len = (int)strlen(str);
            te->msg = new char[len + 1];
            memcpy(te->msg, str, len + 1);
        }
        else te->msg = nullptr;
        te->msg_type = static_cast<uint8>(fields[5].GetUInt32());
        te->msg_lang = static_cast<uint8>(fields[6].GetUInt32());
        te->expire_after = fields[7].GetUInt32();

        std::unordered_map<uint32, TimedEmoteList*>::const_iterator i;
        uint32 spawnid = fields[0].GetUInt32();
        i = m_timedemotes.find(spawnid);
        if (i == m_timedemotes.end())
        {
            TimedEmoteList* m = new TimedEmoteList;
            m->push_back(te);
            m_timedemotes[spawnid] = m;
        }
        else
        {
            i->second->push_back(te);
        }
    }
    while (result->NextRow());

    sLogger.info("ObjectMgr : %u timed emotes cached.", result->GetRowCount());
    delete result;
}

TimedEmoteList* ObjectMgr::GetTimedEmoteList(uint32 spawnid)
{
    std::unordered_map<uint32, TimedEmoteList*>::const_iterator i;
    i = m_timedemotes.find(spawnid);
    if (i != m_timedemotes.end())
    {
        TimedEmoteList* m = i->second;
        return m;
    }
    else return nullptr;
}

Pet* ObjectMgr::CreatePet(uint32 entry)
{
    uint32 guid;
    guid = ++m_hiPetGuid;
    return new Pet(WoWGuid(guid, entry, HIGHGUID_TYPE_PET));
}

Player* ObjectMgr::CreatePlayer(uint8 _class)
{
    uint32_t guid= ++m_hiPlayerGuid;

    return createPlayerByGuid(_class, guid);
}

void ObjectMgr::AddPlayer(Player* p)
{
    std::lock_guard<std::mutex> guard(_playerslock);

    _players[p->getGuidLow()] = p;
}

void ObjectMgr::RemovePlayer(Player* p)
{
    std::lock_guard<std::mutex> guard(_playerslock);

    _players.erase(p->getGuidLow());
}

Corpse* ObjectMgr::CreateCorpse()
{
    uint32 guid;
    guid = ++m_hiCorpseGuid;

    return new Corpse(HIGHGUID_TYPE_CORPSE, guid);
}

void ObjectMgr::AddCorpse(Corpse* p) //add it to global storage
{
    _corpseslock.Acquire();
    m_corpses[p->getGuidLow()] = p;
    _corpseslock.Release();
}

void ObjectMgr::RemoveCorpse(Corpse* p)
{
    _corpseslock.Acquire();
    m_corpses.erase(p->getGuidLow());
    _corpseslock.Release();
}

Corpse* ObjectMgr::GetCorpse(uint32 corpseguid)
{
    Corpse* rv = nullptr;
    _corpseslock.Acquire();
    CorpseMap::const_iterator itr = m_corpses.find(corpseguid);
    rv = (itr != m_corpses.end()) ? itr->second : 0;
    _corpseslock.Release();
    return rv;
}

void ObjectMgr::LoadGuildCharters()
{
    m_hiCharterId = 0;
    QueryResult* result = CharacterDatabase.Query("SELECT * FROM charters");
    if (!result)
        return;
    do
    {
        Charter* c = new Charter(result->Fetch());
        m_charters[c->CharterType].insert(std::make_pair(c->GetID(), c));
        if (c->GetID() > int64(m_hiCharterId.load()))
            m_hiCharterId = c->GetID();
    }
    while (result->NextRow());
    delete result;
    sLogger.info("ObjectMgr : %u charters loaded.", static_cast<uint32_t>(m_charters[0].size()));
}

Charter* ObjectMgr::GetCharter(uint32 CharterId, CharterTypes Type)
{
    std::lock_guard<std::mutex> guard(m_charterLock);

    std::unordered_map<uint32, Charter*>::iterator itr = m_charters[Type].find(CharterId);
    return (itr == m_charters[Type].end()) ? nullptr : itr->second;
}

Charter* ObjectMgr::CreateCharter(uint32 LeaderGuid, CharterTypes Type)
{
    uint32 charterid = 0;
    charterid = ++m_hiCharterId;

    Charter* c = new Charter(charterid, LeaderGuid, Type);
    m_charters[c->CharterType].insert(std::make_pair(c->GetID(), c));

    return c;
}

Charter* ObjectMgr::GetCharterByItemGuid(uint64 guid)
{
    std::lock_guard<std::mutex> guard(m_charterLock);

    for (uint8 i = 0; i < NUM_CHARTER_TYPES; ++i)
    {
        for (std::unordered_map<uint32, Charter*>::iterator itr = m_charters[i].begin(); itr != m_charters[i].end(); ++itr)
        {
            if (itr->second->ItemGuid == guid)
                return itr->second;
        }
    }

    return nullptr;
}

Charter* ObjectMgr::GetCharterByGuid(uint64 playerguid, CharterTypes type)
{
    std::lock_guard<std::mutex> guard(m_charterLock);

    for (std::unordered_map<uint32, Charter*>::iterator itr = m_charters[type].begin(); itr != m_charters[type].end(); ++itr)
    {
        if (playerguid == itr->second->LeaderGuid)
            return itr->second;

        for (uint32 j = 0; j < itr->second->SignatureCount; ++j)
        {
            if (itr->second->Signatures[j] == playerguid)
                return itr->second;
        }
    }

    return nullptr;
}

Charter* ObjectMgr::GetCharterByName(std::string & charter_name, CharterTypes Type)
{
    std::lock_guard<std::mutex> guard(m_charterLock);

    for (std::unordered_map<uint32, Charter*>::iterator itr = m_charters[Type].begin(); itr != m_charters[Type].end(); ++itr)
    {
        if (itr->second->GuildName == charter_name)
            return itr->second;
    }

    return nullptr;
}

void ObjectMgr::RemoveCharter(Charter* c)
{
    if (c == nullptr)
        return;

    if (c->CharterType >= NUM_CHARTER_TYPES)
    {
        sLogger.debug("ObjectMgr : Charter %u cannot be destroyed as type %u is not a sane type value.", c->CharterId, c->CharterType);
        return;
    }

    std::lock_guard<std::mutex> guard(m_charterLock);

    m_charters[c->CharterType].erase(c->CharterId);
}

void ObjectMgr::LoadReputationModifierTable(const char* tablename, ReputationModMap* dmap)
{
    QueryResult* result = WorldDatabase.Query("SELECT * FROM %s", tablename);

    if (result)
    {
        do
        {
            ReputationMod mod;
            mod.faction[TEAM_ALLIANCE] = result->Fetch()[1].GetUInt32();
            mod.faction[TEAM_HORDE] = result->Fetch()[2].GetUInt32();
            mod.value = result->Fetch()[3].GetInt32();
            mod.replimit = result->Fetch()[4].GetUInt32();

            ReputationModMap::iterator itr = dmap->find(result->Fetch()[0].GetUInt32());
            if (itr == dmap->end())
            {
                ReputationModifier* modifier = new ReputationModifier;
                modifier->entry = result->Fetch()[0].GetUInt32();
                modifier->mods.push_back(mod);
                dmap->insert(ReputationModMap::value_type(result->Fetch()[0].GetUInt32(), modifier));
            }
            else
            {
                itr->second->mods.push_back(mod);
            }
        }
        while (result->NextRow());
        delete result;
    }
    sLogger.info("ObjectMgr : %u reputation modifiers on %s.", static_cast<uint32_t>(dmap->size()), tablename);
}

void ObjectMgr::LoadReputationModifiers()
{
    LoadReputationModifierTable("reputation_creature_onkill", &m_reputation_creature);
    LoadReputationModifierTable("reputation_faction_onkill", &m_reputation_faction);
    LoadInstanceReputationModifiers();
}

ReputationModifier* ObjectMgr::GetReputationModifier(uint32 entry_id, uint32 faction_id)
{
    // first, try fetching from the creature table (by faction is a fallback)
    ReputationModMap::iterator itr = m_reputation_creature.find(entry_id);
    if (itr != m_reputation_creature.end())
        return itr->second;

    // fetch from the faction table
    itr = m_reputation_faction.find(faction_id);
    if (itr != m_reputation_faction.end())
        return itr->second;

    // no data. fallback to default -5 value.
    return nullptr;
}

void ObjectMgr::LoadInstanceReputationModifiers()
{
    QueryResult* result = WorldDatabase.Query("SELECT * FROM reputation_instance_onkill");
    if (!result)
        return;
    do
    {
        Field* fields = result->Fetch();
        InstanceReputationMod mod;
        mod.mapid = fields[0].GetUInt32();
        mod.mob_rep_reward = fields[1].GetInt32();
        mod.mob_rep_limit = fields[2].GetUInt32();
        mod.boss_rep_reward = fields[3].GetInt32();
        mod.boss_rep_limit = fields[4].GetUInt32();
        mod.faction[TEAM_ALLIANCE] = fields[5].GetUInt32();
        mod.faction[TEAM_HORDE] = fields[6].GetUInt32();

        std::unordered_map<uint32, InstanceReputationModifier*>::iterator itr = m_reputation_instance.find(mod.mapid);
        if (itr == m_reputation_instance.end())
        {
            InstanceReputationModifier* m = new InstanceReputationModifier;
            m->mapid = mod.mapid;
            m->mods.push_back(mod);
            m_reputation_instance.insert(std::make_pair(m->mapid, m));
        }
        else
            itr->second->mods.push_back(mod);

    }
    while (result->NextRow());
    delete result;

    sLogger.info("ObjectMgr : %u instance reputation modifiers loaded.", static_cast<uint32_t>(m_reputation_instance.size()));
}

bool ObjectMgr::HandleInstanceReputationModifiers(Player* pPlayer, Unit* pVictim)
{
    uint32 team = pPlayer->getTeam();

    if (!pVictim->isCreature())
        return false;

    std::unordered_map<uint32, InstanceReputationModifier*>::iterator itr = m_reputation_instance.find(pVictim->GetMapId());
    if (itr == m_reputation_instance.end())
        return false;

    bool is_boss = false;
    if (static_cast< Creature* >(pVictim)->GetCreatureProperties()->isBoss)
        is_boss = true;

    // Apply the bonuses as normal.
    int32 replimit;
    int32 value;

    for (std::vector<InstanceReputationMod>::iterator i = itr->second->mods.begin(); i != itr->second->mods.end(); ++i)
    {
        if (!(*i).faction[team])
            continue;

        if (is_boss)
        {
            value = i->boss_rep_reward;
            replimit = i->boss_rep_limit;
        }
        else
        {
            value = i->mob_rep_reward;
            replimit = i->mob_rep_limit;
        }

        if (!value || (replimit && pPlayer->GetStanding(i->faction[team]) >= replimit))
            continue;

        //value *= sWorld.getRate(RATE_KILLREPUTATION);
        value = float2int32(value * worldConfig.getFloatRate(RATE_KILLREPUTATION));
        pPlayer->ModStanding(i->faction[team], value);
    }

    return true;
}

void ObjectMgr::LoadDisabledSpells()
{
    QueryResult* result = WorldDatabase.Query("SELECT * FROM spell_disable");
    if (result)
    {
        do
        {
            m_disabled_spells.insert(result->Fetch()[0].GetUInt32());

        } while (result->NextRow());

        delete result;
    }

    sLogger.info("ObjectMgr : %u disabled spells.", static_cast<uint32_t>(m_disabled_spells.size()));
}

void ObjectMgr::ReloadDisabledSpells()
{
    m_disabled_spells.clear();
    LoadDisabledSpells();
}

void ObjectMgr::LoadGroups()
{
    QueryResult* result = CharacterDatabase.Query("SELECT * FROM `groups`");
    if (result)
    {
        if (result->GetFieldCount() != 52)
        {
            sLogger.failure("groups table format is invalid. Please update your database.");
            return;
        }
        do
        {
            Group* g = new Group(false);
            g->LoadFromDB(result->Fetch());
        }
        while (result->NextRow());
        delete result;
    }

    sLogger.info("ObjectMgr : %u groups loaded.", static_cast<uint32_t>(this->m_groups.size()));
}

void ObjectMgr::LoadArenaTeams()
{
    QueryResult* result = CharacterDatabase.Query("SELECT * FROM arenateams");
    if (result != nullptr)
    {
        if (result->GetFieldCount() != 22)
        {
            sLogger.failure("arenateams table format is invalid. Please update your database.");
            return;
        }
        do
        {
            ArenaTeam* team = new ArenaTeam(result->Fetch());
            AddArenaTeam(team);
            if (team->m_id > uint32(m_hiArenaTeamId.load()))
                m_hiArenaTeamId = uint32(team->m_id);

        }
        while (result->NextRow());
        delete result;
    }

    /* update the ranking */
    UpdateArenaTeamRankings();
}

ArenaTeam* ObjectMgr::GetArenaTeamByGuid(uint32 guid, uint32 Type)
{
    m_arenaTeamLock.Acquire();
    for (std::unordered_map<uint32, ArenaTeam*>::iterator itr = m_arenaTeamMap[Type].begin(); itr != m_arenaTeamMap[Type].end(); ++itr)
    {
        if (itr->second->isMember(guid))
        {
            m_arenaTeamLock.Release();
            return itr->second;
        }
    }
    m_arenaTeamLock.Release();
    return nullptr;
}

ArenaTeam* ObjectMgr::GetArenaTeamById(uint32 id)
{
    std::unordered_map<uint32, ArenaTeam*>::iterator itr;
    m_arenaTeamLock.Acquire();
    itr = m_arenaTeams.find(id);
    m_arenaTeamLock.Release();
    return (itr == m_arenaTeams.end()) ? nullptr : itr->second;
}

ArenaTeam* ObjectMgr::GetArenaTeamByName(std::string & name, uint32 /*Type*/)
{
    m_arenaTeamLock.Acquire();
    for (std::unordered_map<uint32, ArenaTeam*>::iterator itr = m_arenaTeams.begin(); itr != m_arenaTeams.end(); ++itr)
    {
        if (!strnicmp(itr->second->m_name.c_str(), name.c_str(), name.size()))
        {
            m_arenaTeamLock.Release();
            return itr->second;
        }
    }
    m_arenaTeamLock.Release();
    return nullptr;
}

void ObjectMgr::RemoveArenaTeam(ArenaTeam* team)
{
    m_arenaTeamLock.Acquire();
    m_arenaTeams.erase(team->m_id);
    m_arenaTeamMap[team->m_type].erase(team->m_id);
    m_arenaTeamLock.Release();
}

void ObjectMgr::AddArenaTeam(ArenaTeam* team)
{
    m_arenaTeamLock.Acquire();
    m_arenaTeams[team->m_id] = team;
    m_arenaTeamMap[team->m_type].insert(std::make_pair(team->m_id, team));
    m_arenaTeamLock.Release();
}

class ArenaSorter
{
    public:

        bool operator()(ArenaTeam* const & a, ArenaTeam* const & b)
        {
            return (a->m_stats.rating > b->m_stats.rating);
        }

        bool operator()(ArenaTeam*& a, ArenaTeam*& b)
        {
            return (a->m_stats.rating > b->m_stats.rating);
        }
};

void ObjectMgr::UpdateArenaTeamRankings()
{
    m_arenaTeamLock.Acquire();
    for (uint8 i = 0; i < NUM_ARENA_TEAM_TYPES; ++i)
    {
        std::vector<ArenaTeam*> ranking;

        for (std::unordered_map<uint32, ArenaTeam*>::iterator itr = m_arenaTeamMap[i].begin(); itr != m_arenaTeamMap[i].end(); ++itr)
            ranking.push_back(itr->second);

        std::sort(ranking.begin(), ranking.end(), ArenaSorter());
        uint32 rank = 1;
        for (std::vector<ArenaTeam*>::iterator itr = ranking.begin(); itr != ranking.end(); ++itr)
        {
            if ((*itr)->m_stats.ranking != rank)
            {
                (*itr)->m_stats.ranking = rank;
                (*itr)->SaveToDB();
            }
            ++rank;
        }
    }
    m_arenaTeamLock.Release();
}

void ObjectMgr::ResetArenaTeamRatings()
{
    m_arenaTeamLock.Acquire();
    for (uint8 i = 0; i < NUM_ARENA_TEAM_TYPES; ++i)
    {
        for (std::unordered_map<uint32, ArenaTeam*>::iterator itr = m_arenaTeamMap[i].begin(); itr != m_arenaTeamMap[i].end(); ++itr)
        {
            ArenaTeam* team = itr->second;
            if (team)
            {
                team->m_stats.played_season = 0;
                team->m_stats.played_week = 0;
                team->m_stats.won_season = 0;
                team->m_stats.won_week = 0;
                team->m_stats.rating = 1500;
                for (uint32 j = 0; j < team->m_memberCount; ++j)
                {
                    team->m_members[j].Played_ThisSeason = 0;
                    team->m_members[j].Played_ThisWeek = 0;
                    team->m_members[j].Won_ThisSeason = 0;
                    team->m_members[j].Won_ThisWeek = 0;
                    team->m_members[j].PersonalRating = 1500;
                }
                team->SaveToDB();
            }
        }
    }
    m_arenaTeamLock.Release();

    UpdateArenaTeamRankings();
}

void ObjectMgr::UpdateArenaTeamWeekly()
{
    // reset weekly matches count for all teams and all members
    m_arenaTeamLock.Acquire();
    for (uint8 i = 0; i < NUM_ARENA_TEAM_TYPES; ++i)
    {
        for (std::unordered_map<uint32, ArenaTeam*>::iterator itr = m_arenaTeamMap[i].begin(); itr != m_arenaTeamMap[i].end(); ++itr)
        {
            ArenaTeam* team = itr->second;
            if (team)
            {
                team->m_stats.played_week = 0;
                team->m_stats.won_week = 0;
                for (uint32 j = 0; j < team->m_memberCount; ++j)
                {
                    team->m_members[j].Played_ThisWeek = 0;
                    team->m_members[j].Won_ThisWeek = 0;
                }
                team->SaveToDB();
            }
        }
    }
    m_arenaTeamLock.Release();
}

void ObjectMgr::ResetDailies()
{
    std::lock_guard<std::mutex> guard(_playerslock);

    for (auto itr : _players)
    {
        if (Player* pPlayer = itr.second)
            pPlayer->resetFinishedDailies();
    }
}

void ObjectMgr::LoadSpellTargetConstraints()
{
    sLogger.info("ObjectMgr : Loading spell target constraints...");

    // Let's try to be idiot proof :/
    QueryResult* result = WorldDatabase.Query("SELECT * FROM spelltargetconstraints WHERE SpellID > 0 ORDER BY SpellID");
    if (result != nullptr)
    {
        uint32 oldspellid = 0;
        SpellTargetConstraint* stc = nullptr;

        do
        {
            Field* fields = result->Fetch();

            if (fields != nullptr)
            {
                uint32 spellid = fields[0].GetUInt32();

                if (oldspellid != spellid)
                {
                    stc = new SpellTargetConstraint;

                    m_spelltargetconstraints.insert(std::pair< uint32, SpellTargetConstraint* >(spellid, stc));
                }

                uint8_t type = fields[1].GetUInt8();
                uint32 value = fields[2].GetUInt32();

                if (type == SPELL_CONSTRAINT_EXPLICIT_CREATURE)
                {
                    if (stc != nullptr)
                    {
                        stc->addCreature(value);
                        stc->addExplicitTarget(value);
                    }
                }
                else if (type == SPELL_CONSTRAINT_EXPLICIT_GAMEOBJECT)
                {
                    if (stc != nullptr)
                    {
                        stc->addGameObject(value);
                        stc->addExplicitTarget(value);
                    }
                }
                else if (type == SPELL_CONSTRAINT_IMPLICIT_CREATURE)
                {
                    if (stc != nullptr)
                    {
                        stc->addCreature(value);
                    }
                }
                else if (type == SPELL_CONSTRAINT_IMPLICIT_GAMEOBJECT)
                {
                    if (stc != nullptr)
                    {
                        stc->addGameObject(value);
                    }
                }

                oldspellid = spellid;
            }
        } while (result->NextRow());
    }

    delete result;

    sLogger.info("ObjectMgr : Loaded constraints for %u spells...", static_cast<uint32_t>(m_spelltargetconstraints.size()));
}

SpellTargetConstraint* ObjectMgr::GetSpellTargetConstraintForSpell(uint32 spellid)
{
    SpellTargetConstraintMap::const_iterator itr = m_spelltargetconstraints.find(spellid);

    if (itr != m_spelltargetconstraints.end())
        return itr->second;
    else
        return nullptr;
}

uint32 ObjectMgr::GenerateArenaTeamId()
{
    uint32 ret;
    ret = ++m_hiArenaTeamId;

    return ret;
}

uint32 ObjectMgr::GenerateGroupId()
{
    uint32 r;
    r = ++m_hiGroupId;

    return r;
}

uint32 ObjectMgr::GenerateGuildId()
{
    uint32 r;
    r = ++m_hiGuildId;

    return r;
}

uint32 ObjectMgr::GenerateCreatureSpawnID()
{
    uint32 r;
    r = ++m_hiCreatureSpawnId;

    return r;
}

uint32 ObjectMgr::GenerateGameObjectSpawnID()
{
    uint32 r;
    r = ++m_hiGameObjectSpawnId;

    return r;
}

void ObjectMgr::LoadVehicleAccessories()
{
    QueryResult* result = WorldDatabase.Query("SELECT creature_entry, accessory_entry, seat FROM vehicle_accessories;");
    if (result != nullptr)
    {
        do
        {
            Field* row = result->Fetch();
            VehicleAccessoryEntry* entry = new VehicleAccessoryEntry();
            uint32 creature_entry = row[0].GetUInt32();
            entry->accessory_entry = row[1].GetUInt32();
            entry->seat = row[2].GetUInt32();

            std::map< uint32, std::vector< VehicleAccessoryEntry* >* >::iterator itr = vehicle_accessories.find(creature_entry);

            if (itr != vehicle_accessories.end())
            {
                itr->second->push_back(entry);
            }
            else
            {
                std::vector< VehicleAccessoryEntry* >* v = new std::vector< VehicleAccessoryEntry* >();
                v->push_back(entry);
                vehicle_accessories.insert(std::make_pair(creature_entry, v));
            }

        } while (result->NextRow());

        delete result;
    }
}

std::vector< VehicleAccessoryEntry* >* ObjectMgr::GetVehicleAccessories(uint32 creature_entry)
{
    std::map< uint32, std::vector< VehicleAccessoryEntry* >* >::iterator itr = vehicle_accessories.find(creature_entry);

    if (itr == vehicle_accessories.end())
        return nullptr;
    else
        return itr->second;
}

void ObjectMgr::LoadWorldStateTemplates()
{
    QueryResult* result = WorldDatabase.QueryNA("SELECT DISTINCT map FROM worldstate_templates ORDER BY map;");
    if (result == nullptr)
        return;

    do
    {
        Field* row = result->Fetch();
        uint32 mapid = row[0].GetUInt32();

        worldstate_templates.insert(std::make_pair(mapid, new std::multimap< uint32, WorldState >()));

    } while (result->NextRow());

    delete result;

    result = WorldDatabase.QueryNA("SELECT map, zone, field, value FROM worldstate_templates;");
    if (result == nullptr)
        return;

    do
    {
        Field* row = result->Fetch();
        WorldState ws;

        uint32 mapid = row[0].GetUInt32();
        uint32 zone = row[1].GetUInt32();
        ws.field = row[2].GetUInt32();
        ws.value = row[3].GetUInt32();

        std::map< uint32, std::multimap< uint32, WorldState >* >::iterator itr = worldstate_templates.find(mapid);
        if (itr == worldstate_templates.end())
            continue;

        itr->second->insert(std::make_pair(zone, ws));

    } while (result->NextRow());

    delete result;
}

std::multimap< uint32, WorldState >* ObjectMgr::GetWorldStatesForMap(uint32 map) const
{
    std::map< uint32, std::multimap< uint32, WorldState >* >::const_iterator itr = worldstate_templates.find(map);

    if (itr == worldstate_templates.end())
        return nullptr;
    else
        return itr->second;
}

void ObjectMgr::LoadEventScripts()
{
    sLogger.info("ObjectMgr : Loading Event Scripts...");

    bool success = false;
    const char* eventScriptsQuery = "SELECT `event_id`, `function`, `script_type`, `data_1`, `data_2`, `data_3`, `data_4`, `data_5`, `x`, `y`, `z`, `o`, `delay`, `next_event` FROM `event_scripts` WHERE `event_id` > 0 ORDER BY `event_id`";
    auto result = WorldDatabase.Query(&success, eventScriptsQuery);

    if (!success)
    {
        sLogger.debug("LoadEventScripts : Failed on Loading Queries from event_scripts.");
        return;
    }
    else
    {
        if (!result)
        {
            sLogger.debug("LoadEventScripts : Loaded 0 event_scripts. DB table `event_scripts` is empty.");
            return;
        }
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        uint32 event_id = fields[0].GetUInt32();
        SimpleEventScript eventscript;

        eventscript.eventId     = event_id;
        eventscript.function    = static_cast<uint8>(ScriptCommands(fields[1].GetUInt8()));
        eventscript.scripttype  = static_cast<uint8>(EasyScriptTypes(fields[2].GetUInt8()));
        eventscript.data_1      = fields[3].GetUInt32();
        eventscript.data_2      = fields[4].GetUInt32();
        eventscript.data_3      = fields[5].GetUInt32();
        eventscript.data_4      = fields[6].GetUInt32();
        eventscript.data_5      = fields[7].GetUInt32();
        eventscript.x           = fields[8].GetUInt32();
        eventscript.y           = fields[9].GetUInt32();
        eventscript.z           = fields[10].GetUInt32();
        eventscript.o           = fields[11].GetUInt32();
        eventscript.delay       = fields[12].GetUInt32();
        eventscript.nextevent   = fields[13].GetUInt32();

        SimpleEventScript* SimpleEventScript = &mEventScriptMaps.insert(EventScriptMaps::value_type(event_id, eventscript))->second;

        // for search by spellid ( data_1 is spell id )
        if (eventscript.data_1 && eventscript.scripttype == static_cast<uint8>(EasyScriptTypes::SCRIPT_TYPE_SPELL_EFFECT))
            mSpellEffectMaps.insert(SpellEffectMaps::value_type(eventscript.data_1, SimpleEventScript));


        ++count;

    } while (result->NextRow());

    delete result;

    sLogger.info("ObjectMgr : Loaded event_scripts for %u events...", count);
}

EventScriptBounds ObjectMgr::GetEventScripts(uint32 event_id) const
{
    return EventScriptBounds(mEventScriptMaps.lower_bound(event_id), mEventScriptMaps.upper_bound(event_id));
}

SpellEffectMapBounds ObjectMgr::GetSpellEffectBounds(uint32 data_1) const
{
    return SpellEffectMapBounds(mSpellEffectMaps.lower_bound(data_1), mSpellEffectMaps.upper_bound(data_1));
}

bool ObjectMgr::CheckforScripts(Player* plr, uint32 event_id)
{
    EventScriptBounds EventScript = sObjectMgr.GetEventScripts(event_id);
    if (EventScript.first == EventScript.second)
        return false;

    for (EventScriptMaps::const_iterator itr = EventScript.first; itr != EventScript.second; ++itr)
    {
        sEventMgr.AddEvent(this, &ObjectMgr::EventScriptsUpdate, plr, itr->second.eventId, EVENT_EVENT_SCRIPTS, itr->second.delay, 1, 0);
    }

    return true;
}

bool ObjectMgr::CheckforDummySpellScripts(Player* plr, uint32 data_1)
{
    SpellEffectMapBounds EventScript = sObjectMgr.GetSpellEffectBounds(data_1);
    if (EventScript.first == EventScript.second)
        return false;

    for (SpellEffectMaps::const_iterator itr = EventScript.first; itr != EventScript.second; ++itr)
    {
        sEventMgr.AddEvent(this, &ObjectMgr::EventScriptsUpdate, plr, itr->second->eventId, EVENT_EVENT_SCRIPTS, itr->second->delay, 1, 0);
    }

    return true;
}

void ObjectMgr::EventScriptsUpdate(Player* plr, uint32 next_event)
{
    EventScriptBounds EventScript = sObjectMgr.GetEventScripts(next_event);

    for (EventScriptMaps::const_iterator itr = EventScript.first; itr != EventScript.second; ++itr)
    {
        if (itr->second.scripttype == static_cast<uint8>(EasyScriptTypes::SCRIPT_TYPE_SPELL_EFFECT) || itr->second.scripttype == static_cast<uint8>(EasyScriptTypes::SCRIPT_TYPE_DUMMY))
        {
            switch (itr->second.function)
            {
            case static_cast<uint8>(ScriptCommands::SCRIPT_COMMAND_RESPAWN_GAMEOBJECT):
            {
                Object* target = plr->GetMapMgr()->GetInterface()->GetGameObjectNearestCoords(plr->GetPositionX(), plr->GetPositionY(), plr->GetPositionZ(), itr->second.data_1);
                if (target == nullptr)
                    return;

                static_cast<GameObject*>(target)->Despawn(1000, itr->second.data_2);

                break;
            }

            case static_cast<uint8>(ScriptCommands::SCRIPT_COMMAND_KILL_CREDIT):
            {
                if (auto* questLog = plr->getQuestLogByQuestId(itr->second.data_2))
                {
                    if (questLog->getQuestProperties()->required_mob_or_go[itr->second.data_5] >= 0)
                    {
                        uint32 required_mob = static_cast<uint32>(questLog->getQuestProperties()->required_mob_or_go[itr->second.data_5]);
                        const auto index = static_cast<uint8_t>(itr->second.data_5);
                        if (questLog->getMobCountByIndex(index) < required_mob)
                        {
                            questLog->setMobCountForIndex(index, questLog->getMobCountByIndex(index) + 1);
                            questLog->SendUpdateAddKill(index);
                            questLog->updatePlayerFields();
                        }
                    }
                }
                break;
            }
            }
        }

        if (itr->second.scripttype == static_cast<uint8>(EasyScriptTypes::SCRIPT_TYPE_GAMEOBJECT) || itr->second.scripttype == static_cast<uint8>(EasyScriptTypes::SCRIPT_TYPE_DUMMY))
        {
            switch (itr->second.function)
            {
            case static_cast<uint8>(ScriptCommands::SCRIPT_COMMAND_ACTIVATE_OBJECT):
            {
                if ((itr->second.x || itr->second.y || itr->second.z) == 0)
                {
                    Object* target = plr->GetMapMgr()->GetInterface()->GetGameObjectNearestCoords(plr->GetPositionX(), plr->GetPositionY(), plr->GetPositionZ(), itr->second.data_1);
                    if (target == nullptr)
                        return;

                    if (static_cast<GameObject*>(target)->getState() != GO_STATE_OPEN)
                    {
                        static_cast<GameObject*>(target)->setState(GO_STATE_OPEN);
                    }
                    else
                    {
                        static_cast<GameObject*>(target)->setState(GO_STATE_CLOSED);
                    }
                }
                else
                {
                    Object* target = plr->GetMapMgr()->GetInterface()->GetGameObjectNearestCoords(float(itr->second.x), float(itr->second.y), float(itr->second.z), itr->second.data_1);
                    if (target == nullptr)
                        return;

                    if (static_cast<GameObject*>(target)->getState() != GO_STATE_OPEN)
                    {
                        static_cast<GameObject*>(target)->setState(GO_STATE_OPEN);
                    }
                    else
                    {
                        static_cast<GameObject*>(target)->setState(GO_STATE_CLOSED);
                    }
                }
            }
            break;
            }
        }

        if (itr->second.nextevent != 0)
        {
            sObjectMgr.CheckforScripts(plr, itr->second.nextevent);
        }
    }
}

void ObjectMgr::LoadInstanceEncounters()
{
    const auto startTime = Util::TimeNow();

    //                                                 0         1            2                3               4       5
    QueryResult* result = WorldDatabase.Query("SELECT entry, creditType, creditEntry, lastEncounterDungeon, comment, mapid FROM instance_encounters");
    if (result == nullptr)
    {
        sLogger.debug(">> Loaded 0 instance encounters, table is empty!");
        return;
    }

#if VERSION_STRING >= WotLK
    std::map<uint32_t, DBC::Structures::DungeonEncounterEntry const*> dungeonLastBosses;
#endif

    uint32_t count = 0;
    do
    {
        Field* fields = result->Fetch();
        auto entry = fields[0].GetUInt32();
        auto creditType = fields[1].GetUInt8();
        auto creditEntry = fields[2].GetUInt32();
        auto lastEncounterDungeon = fields[3].GetUInt16();
        auto dungeonEncounterName = fields[4].GetString();

#if VERSION_STRING <= TBC
        auto mapId = fields[5].GetUInt32();
#else
        const auto dungeonEncounter = sDungeonEncounterStore.LookupEntry(entry);
        if (dungeonEncounter == nullptr)
        {
            sLogger.debugFlag(AscEmu::Logging::LF_DB_TABLES, "Table `instance_encounters` has an invalid encounter id %u, skipped!", entry);
            continue;
        }

#if VERSION_STRING == WotLK
        dungeonEncounterName = dungeonEncounter->encounterName[0];
#else
        dungeonEncounterName = dungeonEncounter->encounterName;
#endif
#endif

        if (lastEncounterDungeon && sLfgMgr.GetLFGDungeon(lastEncounterDungeon) == 0)
        {
            sLogger.debugFlag(AscEmu::Logging::LF_DB_TABLES, "Table `instance_encounters` has an encounter %u (%s) marked as final for invalid dungeon id %u, skipped!", entry, dungeonEncounterName, lastEncounterDungeon);
            continue;
        }

#if VERSION_STRING >= WotLK
        if (lastEncounterDungeon)
        {
            const auto itr = dungeonLastBosses.find(lastEncounterDungeon);
            if (itr != dungeonLastBosses.end())
            {
#if VERSION_STRING == WotLK
                const auto itrEncounterName = itr->second->encounterName[0];
#else
                const auto itrEncounterName = itr->second->encounterName;
#endif
                sLogger.debugFlag(AscEmu::Logging::LF_DB_TABLES, "Table `instance_encounters` specified encounter %u (%s) as last encounter but %u (%s) is already marked as one, skipped!", entry, dungeonEncounterName, itr->second->id, itrEncounterName);
                continue;
            }

            dungeonLastBosses[lastEncounterDungeon] = dungeonEncounter;
        }
#endif

        switch (creditType)
        {
            case ENCOUNTER_CREDIT_KILL_CREATURE:
            {
                auto creatureprop = sMySQLStore.getCreatureProperties(creditEntry);
                if (creatureprop == nullptr)
                {
                    sLogger.debugFlag(AscEmu::Logging::LF_DB_TABLES, "Table `instance_encounters` has an invalid creature (entry %u) linked to the encounter %u (%s), skipped!", creditEntry, entry, dungeonEncounterName);
                    continue;
                }
                const_cast<CreatureProperties*>(creatureprop)->extra_a9_flags |= 0x10000000; // Flagged Dungeon Boss
                break;
            }
            case ENCOUNTER_CREDIT_CAST_SPELL:
            {
                if (sSpellMgr.getSpellInfo(creditEntry) == nullptr)
                {
                    sLogger.debugFlag(AscEmu::Logging::LF_DB_TABLES, "Table `instance_encounters` has an invalid spell (entry %u) linked to the encounter %u (%s), skipped!", creditEntry, entry, dungeonEncounterName);
                    continue;
                }
                break;
            }
            default:
            {
                sLogger.debugFlag(AscEmu::Logging::LF_DB_TABLES, "Table `instance_encounters` has an invalid credit type (%u) for encounter %u (%s), skipped!", creditType, entry, dungeonEncounterName);
                continue;
            }
        }

#if VERSION_STRING <= TBC
        DungeonEncounterList& encounters = _dungeonEncounterStore[mapId];
        encounters.push_back(new DungeonEncounter(EncounterCreditType(creditType), creditEntry));
#else
        DungeonEncounterList& encounters = _dungeonEncounterStore[static_cast<int32_t>(static_cast<uint16_t>(dungeonEncounter->mapId) | (static_cast<uint32_t>(dungeonEncounter->difficulty) << 16))];
        encounters.push_back(new DungeonEncounter(dungeonEncounter, EncounterCreditType(creditType), creditEntry, lastEncounterDungeon));
#endif
        ++count;
    } while (result->NextRow());

    sLogger.info("ObjectMgr : Loaded %u instance encounters in %u ms", count, static_cast<uint32_t>(Util::GetTimeDifferenceToNow(startTime)));
}

void ObjectMgr::loadCreatureMovementOverrides()
{
    const auto startTime = Util::TimeNow();
    uint32_t count = 0;

    _creatureMovementOverrides.clear();

    QueryResult* result = WorldDatabase.Query("SELECT SpawnId, Ground, Swim, Flight, Rooted, Chase, Random from creature_movement_override");

    if (!result)
    {
        sLogger.info(">> Loaded 0 creature movement overrides. DB table `creature_movement_override` is empty!");
        return;
    }

    do
    {
        Field* fields = result->Fetch();
        uint32_t spawnId = fields[0].GetUInt32();

        QueryResult* spawnResult = WorldDatabase.Query("SELECT * FROM creature_spawns WHERE id = %u", spawnId);
        if (spawnResult == nullptr)
        {
            sLogger.failure("Creature (SpawnId: %u) does not exist but has a record in `creature_movement_override`", spawnId);
            continue;
        }

        CreatureMovementData& movement = _creatureMovementOverrides[spawnId];
        movement.Ground = static_cast<CreatureGroundMovementType>(fields[1].GetUInt8());
        movement.Swim = fields[2].GetBool();
        movement.Flight = static_cast<CreatureFlightMovementType>(fields[3].GetUInt8());
        movement.Rooted = fields[4].GetBool();
        movement.Chase = static_cast<CreatureChaseMovementType>(fields[5].GetUInt8());
        movement.Random = static_cast<CreatureRandomMovementType>(fields[6].GetUInt8());

        checkCreatureMovement(spawnId, movement);
        ++count;
    } while (result->NextRow());

    sLogger.info("ObjectMgr :  Loaded %u movement overrides in %u ms", count, static_cast<uint32_t>(Util::GetTimeDifferenceToNow(startTime)));
}

void ObjectMgr::checkCreatureMovement(uint32_t /*id*/, CreatureMovementData& creatureMovement)
{
    if (creatureMovement.Ground >= CreatureGroundMovementType::Max)
    {
        creatureMovement.Ground = CreatureGroundMovementType::Run;
    }

    if (creatureMovement.Flight >= CreatureFlightMovementType::Max)
    {
        creatureMovement.Flight = CreatureFlightMovementType::None;
    }

    if (creatureMovement.Chase >= CreatureChaseMovementType::Max)
    {
        creatureMovement.Chase = CreatureChaseMovementType::Run;
    }

    if (creatureMovement.Random >= CreatureRandomMovementType::Max)
    {
        creatureMovement.Random = CreatureRandomMovementType::Walk;
    }
}

#if VERSION_STRING > WotLK
uint64_t ObjectMgr::generateVoidStorageItemId()
{
    return ++m_voidItemId;
}
#endif
