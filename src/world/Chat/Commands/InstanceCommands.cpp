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


#include "Server/MainServerDefines.h"
#include "Map/InstanceDefines.hpp"
#include "Map/MapMgr.h"
#include "Map/WorldCreatorDefines.hpp"
#include "Map/WorldCreator.h"
#include "Chat/ChatHandler.hpp"
#include "Management/ObjectMgr.h"
#include "Server/Packets/SmsgInstanceReset.h"
#include "Server/Script/ScriptMgr.h"

using namespace AscEmu::Packets;

//.instance create
bool ChatHandler::HandleCreateInstanceCommand(const char* args, WorldSession* m_session)
{
    Player* plr = GetSelectedPlayer(m_session, true, true);
    if (plr == nullptr)
        return true;

    float x, y, z;
    uint32 mapid;

    if (sscanf(args, "%u %f %f %f", &mapid, &x, &y, &z) != 4)
        return false;

    // Create Map Manager
    MapMgr* mgr = sInstanceMgr.CreateInstance(INSTANCE_DUNGEON, mapid);
    if (mgr == nullptr)
    {
        sLogger.failure("call failed for map %u", mapid);
        return false;
    }
    sLogger.info("CreateInstanceGMCommand : GM created instance for map %u", mapid);

    LocationVector vec(x, y, z);
    m_session->GetPlayer()->SafeTeleport(mgr, vec);

    return true;
}

//.instance countcreature
bool ChatHandler::HandleCountCreaturesCommand(const char* args, WorldSession* m_session)
{
    Player* plr = m_session->GetPlayer();
    if (plr == nullptr)
        return true;

    uint32 entry;
    if (sscanf(args, "%u", &entry) != 1)
        return false;

    Instance* instance = sInstanceMgr.GetInstanceByIds(MAX_NUM_MAPS, plr->GetInstanceID());
    if (instance == nullptr)
        return true;

    instance->m_mapMgr->GetScript()->getCreatureSetForEntry(entry, true, plr);

    return true;
}

//.instance exit
bool ChatHandler::HandleExitInstanceCommand(const char* /*args*/, WorldSession* m_session)
{
    BlueSystemMessage(m_session, "Attempting to exit from instance...");

    bool result = m_session->GetPlayer()->ExitInstance();
    if (!result)
        RedSystemMessage(m_session, "Entry points not found.");
    else
        GreenSystemMessage(m_session, "Removal successful.");

    return true;
}

//.instance info
bool ChatHandler::HandleGetInstanceInfoCommand(const char* args, WorldSession* m_session)
{
    Player* plr = m_session->GetPlayer();
    if (plr == nullptr)
        return false;

    bool userInput = true;
    uint32 instanceId = (args ? atoi(args) : 0);
    if (instanceId == 0)
    {
        userInput = false;
        instanceId = plr->GetInstanceID();
        if (instanceId == 0)
            return false;
    }

    Instance* instance = sInstanceMgr.GetInstanceByIds(MAX_NUM_MAPS, instanceId);
    if (instance == nullptr)
    {
        if (userInput)
        {
            RedSystemMessage(m_session, "Instance with id %u not found.", instanceId);
            return true;
        }
        return false;
    }

    std::stringstream ss;
    ss << "Instance ID: " << MSG_COLOR_CYAN << instance->m_instanceId << "|r (" << MSG_COLOR_CYAN;
    if (instance->m_mapInfo == nullptr)
        ss << instance->m_mapId;
    else
        ss << instance->m_mapInfo->name;
    ss << "|r)\n";
    ss << "Persistent: " << MSG_COLOR_CYAN << (instance->m_persistent ? "Yes" : "No") << "|r\n";
    if (instance->m_mapInfo != nullptr)
    {
        ss << "Type: " << MSG_COLOR_CYAN << GetMapTypeString(static_cast<uint8>(instance->m_mapInfo->type)) << "|r";

        if (instance->m_mapInfo->isMultimodeDungeon())
        {
            ss << " (" << MSG_COLOR_CYAN << GetDifficultyString(instance->m_difficulty) << "|r)";
        }

        if (instance->m_mapInfo->isRaid())
        {
            ss << " (" << MSG_COLOR_CYAN << GetRaidDifficultyString(instance->m_difficulty) << "|r)";
        }

        ss << "\n";
    }
    ss << "Created: " << MSG_COLOR_CYAN << Util::GetDateTimeStringFromTimeStamp(instance->m_creation) << "|r\n";
    if (instance->m_expiration != 0)
        ss << "Expires: " << MSG_COLOR_CYAN << Util::GetDateTimeStringFromTimeStamp((uint32)instance->m_expiration) << "|r\n";

    if (instance->m_mapMgr == NULL)
    {
        ss << "Status: " << MSG_COLOR_LIGHTRED << "Shut Down|r\n";
    }
    else if (!instance->m_mapMgr->HasPlayers())
    {
        ss << "Status: " << MSG_COLOR_LIGHTRED << "Idle|r";
        if (instance->m_mapMgr->InactiveMoveTime && !instance->m_mapMgr->GetMapInfo()->isNonInstanceMap())
            ss << " (" << MSG_COLOR_CYAN << "Shutdown in " << MSG_COLOR_LIGHTRED << (((long)instance->m_mapMgr->InactiveMoveTime - UNIXTIME) / 60) << MSG_COLOR_CYAN << " minutes|r)";
        ss << "\n";
    }
    else
    {
        ss << "Status: " << MSG_COLOR_GREEN << "In use|r (" << MSG_COLOR_GREEN << instance->m_mapMgr->GetPlayerCount() << MSG_COLOR_CYAN << " players inside|r)\n";

    }
    SendMultilineMessage(m_session, ss.str().c_str());

    if (instance->m_mapMgr != nullptr && instance->m_mapMgr->GetScript() != nullptr)
        instance->m_mapMgr->GetScript()->displayDataStateList(plr);

    return true;
}

//.instance reset
bool ChatHandler::HandleResetInstanceCommand(const char* args, WorldSession* m_session)
{

    uint32 instanceId;
    int argc = 1;
    char* playername = NULL;
    char* guidString = (char*)args;

    // Parse arguments
    char* space = (char*)strchr(args, ' ');
    if (space)
    {
        *space = '\0';
        playername = space + 1;
        argc = 2;
    }

    instanceId = atoi(guidString);
    if (!instanceId)
    {
        RedSystemMessage(m_session, "You must specify an instance id.");
        return true;
    }


    Player* plr;

    if (argc == 1)
        plr = GetSelectedPlayer(m_session, true, true);
    else
        plr = sObjectMgr.GetPlayer(playername, false);

    if (!plr)
    {
        RedSystemMessage(m_session, "Player not found");
        return true;
    }

    Instance* instance = sInstanceMgr.GetInstanceByIds(MAX_NUM_MAPS, instanceId);
    if (instance == nullptr)
    {
        RedSystemMessage(m_session, "There's no instance with id %u.", instanceId);
        return true;
    }

    if (instance->isPersistent())
    {
        if (m_session->CanUseCommand('z'))
        {
            bool foundSomething = false;
            std::lock_guard<std::mutex> lock(plr->getPlayerInfo()->savedInstanceIdsLock);
            for (uint8 difficulty = 0; difficulty < InstanceDifficulty::MAX_DIFFICULTY; difficulty++)
            {
                PlayerInstanceMap::iterator itr = plr->getPlayerInfo()->savedInstanceIds[difficulty].find(instance->m_mapId);
                if (itr == plr->getPlayerInfo()->savedInstanceIds[difficulty].end() || (*itr).second != instance->m_instanceId)
                    continue;
                plr->SetPersistentInstanceId(instance->m_mapId, difficulty, 0);
                SystemMessage(m_session, "Instance with id %u (%s) is persistent and will only be revoked from player.", instanceId, GetDifficultyString(difficulty));
                foundSomething = true;
            }

            if (!foundSomething)
                RedSystemMessage(m_session, "Player is not assigned to persistent instance with id %u.", instanceId);
            return true;
        }
        else
        {
            RedSystemMessage(m_session, "Instance with id %u is persistent and can only be removed from player by admins.", instanceId);
            return true;
        }
    }

    if (instance->m_mapMgr && instance->m_mapMgr->HasPlayers())
    {
        RedSystemMessage(m_session, "Failed to reset non-persistent instance with id %u, due to player still inside.", instanceId);
        return true;
    }

    if (instance->m_creatorGroup)
    {
        Group* group = plr->getGroup();
        if (group == nullptr || instance->m_creatorGroup != group->GetID())
        {
            RedSystemMessage(m_session, "Player %s is not a member of the group assigned to the non-persistent instance with id %u.", plr->getName().c_str(), instanceId);
            return true;
        }
    }
    else if (instance->m_creatorGuid == 0 || instance->m_creatorGuid != plr->getGuidLow())
    {
        RedSystemMessage(m_session, "Player %s is not assigned to instance with id %u.", plr->getName().c_str(), instanceId);
        return true;
    }

    // tell player the instance was reset
    plr->GetSession()->SendPacket(SmsgInstanceReset(instance->m_mapId).serialise().get());

    // shut down instance
    sInstanceMgr.DeleteBattlegroundInstance(instance->m_mapId, instance->m_instanceId);
    //    RedSystemMessage(m_session, "Resetting single non-persistent instances is not available yet.");
    sGMLog.writefromsession(m_session, "used reset instance command on %s, instance %u,", plr->getName().c_str(), instanceId);
    return true;
}

//.instance resetall
bool ChatHandler::HandleResetAllInstancesCommand(const char* args, WorldSession* m_session)
{
    bool is_name_set = false;
    Player* player;

    if (*args)
        is_name_set = true;

    if (is_name_set)
    {
        player = sObjectMgr.GetPlayer(args, false);
        if (player == nullptr)
        {
            RedSystemMessage(m_session, "Player %s is not online or does not exist!", args);
            return true;
        }
    }
    else
    {
        player = GetSelectedPlayer(m_session, true);
        if (player == nullptr)
            return true;
    }

    SystemMessage(m_session, "Trying to reset all instances of player %s...", player->getName().c_str());
    sInstanceMgr.ResetSavedInstances(player);
    SystemMessage(m_session, "...done");

    sGMLog.writefromsession(m_session, "used reset all instances command on %s,", player->getName().c_str());
    return true;
}

//.instance shutdown
bool ChatHandler::HandleShutdownInstanceCommand(const char* args, WorldSession* m_session)
{
    uint32 instanceId = (args ? atoi(args) : 0);
    if (instanceId == 0)
        return false;

    Instance* instance = sInstanceMgr.GetInstanceByIds(MAX_NUM_MAPS, instanceId);
    if (instance == nullptr)
    {
        RedSystemMessage(m_session, "There's no instance with id %u.", instanceId);
        return true;
    }

    if (instance->m_mapMgr == nullptr)
    {
        RedSystemMessage(m_session, "Instance with id %u already shut down.", instanceId);
        return true;
    }

    SystemMessage(m_session, "Attempting to shutdown instance with id %u...", instanceId);

    sInstanceMgr.SafeDeleteInstance(instance->m_mapMgr);

    instance = nullptr;

    SystemMessage(m_session, "...done");

    sGMLog.writefromsession(m_session, "used shutdown instance command on instance %u,", instanceId);

    return true;
}

//.instance showtimers
bool ChatHandler::HandleShowTimersCommand(const char* /*args*/, WorldSession* m_session)
{
    Player* player = m_session->GetPlayer();
    if (player == nullptr)
        return true;

    uint32_t instanceId = player->GetInstanceID();
    if (instanceId == 0)
        return true;

    Instance* instance = sInstanceMgr.GetInstanceByIds(MAX_NUM_MAPS, instanceId);

    if (instance && instance->m_mapMgr != nullptr && instance->m_mapMgr->GetScript() != nullptr)
        instance->m_mapMgr->GetScript()->displayTimerList(player);

    return true;
}
