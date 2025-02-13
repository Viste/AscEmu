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

#include "TLSObject.h"
#include "Objects/DynamicObject.h"
#include "CellHandler.h"
#include "CThreads.h"
#include "Management/WorldStatesHandler.h"
#include "Objects/Item.h"
#include "Map/Area/AreaStorage.hpp"
#include "CrashHandler.h"
#include "Objects/Units/Creatures/Summons/Summon.h"
#include "Objects/Units/Unit.h"
#include "VMapFactory.h"
#include "MMapFactory.h"
#include "Storage/MySQLDataStore.hpp"
#include "Macros/ScriptMacros.hpp"
#include "MapMgr.h"
#include "MapScriptInterface.h"
#include "WorldCreator.h"
#include "Objects/Units/Creatures/Pet.h"
#include "Server/Packets/SmsgUpdateWorldState.h"
#include "Server/Packets/SmsgDefenseMessage.h"
#include "Server/Script/ScriptMgr.h"

#include "shared/WoWGuid.h"

using namespace AscEmu::Packets;

Arcemu::Utility::TLSObject<MapMgr*> t_currentMapContext;

extern bool bServerShutdown;

MapMgr::MapMgr(Map* map, uint32 mapId, uint32 instanceid) : CellHandler<MapCell>(map), _mapId(mapId), eventHolder(instanceid), worldstateshandler(mapId)
{
    _terrain = new TerrainHolder(mapId);
    _shutdown = false;
    m_instanceID = instanceid;
    pMapInfo = sMySQLStore.getWorldMapInfo(mapId);
    m_UpdateDistance = pMapInfo->update_distance * pMapInfo->update_distance;
    iInstanceMode = 0;

    // Create script interface
    ScriptInterface = new MapScriptInterface(*this);

    // Set up storage arrays
    CreatureStorage.resize(map->CreatureSpawnCount, nullptr);
    GOStorage.resize(map->GameObjectSpawnCount, nullptr);

    m_TransportStorage.clear();

    m_GOHighGuid = m_CreatureHighGuid = 0;
    m_DynamicObjectHighGuid = 0;
    lastTransportUpdate = Util::getMSTime();
    lastUnitUpdate = Util::getMSTime();
    lastGameobjectUpdate = Util::getMSTime();
    lastDynamicObjectUpdate = Util::getMSTime();
    m_battleground = nullptr;

    m_holder = &eventHolder;
    m_event_Instanceid = eventHolder.GetInstanceID();
    forced_expire = false;
    InactiveMoveTime = 0;
    mLoopCounter = 0;
    pInstance = nullptr;
    thread_kill_only = false;
    thread_running = false;

    m_forcedcells.clear();
    m_PlayerStorage.clear();
    m_PetStorage.clear();
    m_DynamicObjectStorage.clear();

    _combatProgress.clear();
    _mapWideStaticObjects.clear();
    //_worldStateSet.clear();
    _updates.clear();
    _processQueue.clear();
    Sessions.clear();

    activeGameObjects.clear();
    activeCreatures.clear();
    creature_iterator = activeCreatures.begin();
    pet_iterator = m_PetStorage.begin();
    m_corpses.clear();
    _sqlids_creatures.clear();
    _sqlids_gameobjects.clear();
    _reusable_guids_gameobject.clear();
    _reusable_guids_creature.clear();

    mInstanceScript = NULL;
}

MapMgr::~MapMgr()
{
    _shutdown = true;
    sEventMgr.RemoveEvents(this);
    if (ScriptInterface != nullptr)
    {
        delete ScriptInterface;
        ScriptInterface = nullptr;
    }

    delete _terrain;

    // Remove objects
    if (_cells)
    {
        for (uint32 i = 0; i < _sizeX; i++)
        {
            if (_cells[i] != 0)
            {
                for (uint32 j = 0; j < _sizeY; j++)
                {
                    if (_cells[i][j] != 0)
                    {
                        _cells[i][j]->_unloadpending = false;
                        _cells[i][j]->RemoveObjects();
                    }
                }
            }
        }
    }

    for (auto _mapWideStaticObject : _mapWideStaticObjects)
    {
        if (_mapWideStaticObject->IsInWorld())
            _mapWideStaticObject->RemoveFromWorld(false);
        delete _mapWideStaticObject;
    }
    _mapWideStaticObjects.clear();

    GOStorage.clear();
    CreatureStorage.clear();
    m_TransportStorage.clear();

    for (auto itr = m_corpses.begin(); itr != m_corpses.end();)
    {
        Corpse* pCorpse = *itr;
        ++itr;

        if (pCorpse->IsInWorld())
            pCorpse->RemoveFromWorld(false);

        delete pCorpse;
    }
    m_corpses.clear();

    if (mInstanceScript != NULL)
        mInstanceScript->Destroy();

    // Empty remaining containers
    m_PlayerStorage.clear();
    m_PetStorage.clear();
    m_DynamicObjectStorage.clear();

    _combatProgress.clear();
    _updates.clear();
    _processQueue.clear();
    Sessions.clear();

    activeCreatures.clear();
    activeGameObjects.clear();
    _sqlids_creatures.clear();
    _sqlids_gameobjects.clear();
    _reusable_guids_creature.clear();
    _reusable_guids_gameobject.clear();

    if (m_battleground)
    {
        m_battleground = nullptr;
    }

    MMAP::MMapFactory::createOrGetMMapManager()->unloadMapInstance(GetMapId(), m_instanceID);

    sLogger.debug("MapMgr : Instance %u shut down. (%s)", m_instanceID, GetBaseMap()->GetMapName().c_str());
}

void MapMgr::PushObject(Object* obj)
{
    if (obj != nullptr)
    {
        //\todo That object types are not map objects. TODO: add AI groups here?
        if (obj->isItem() || obj->isContainer())
        {
            // mark object as updatable and exit
            return;
        }

        //Zyres: this was an old ASSERT MapMgr for map x is not allowed to push objects for mapId z
        if (obj->GetMapId() != _mapId)
        {
            sLogger.failure("MapMgr::PushObject manager for mapId %u tried to push object for mapId %u, return!", _mapId, obj->GetMapId());
            return;
        }

        if (obj->GetPositionY() > _maxY || obj->GetPositionY() < _minY)
        {
            sLogger.failure("MapMgr::PushObject not allowed to push object to y: %f (max %f/min %f), return!", obj->GetPositionY(), _maxY, _minY);
            return;
        }

        if (_cells == nullptr)
        {
            sLogger.failure("MapMgr::PushObject not allowed to push object to invalid cell (nullptr), return!");
            return;
        }

        if (obj->isCorpse())
        {
            m_corpses.insert(static_cast<Corpse*>(obj));
        }

        obj->clearInRangeSets();

        // Check valid cell x/y values
        if (!(obj->GetPositionX() < _maxX && obj->GetPositionX() > _minX) || !(obj->GetPositionY() < _maxY && obj->GetPositionY() > _minY))
        {
            OutOfMapBoundariesTeleport(obj);
        }

        // Get cell coordinates
        uint32 x = GetPosX(obj->GetPositionX());
        uint32 y = GetPosY(obj->GetPositionY());

        if (x >= _sizeX || y >= _sizeY)
        {
            OutOfMapBoundariesTeleport(obj);

            x = GetPosX(obj->GetPositionX());
            y = GetPosY(obj->GetPositionY());
        }

        MapCell* objCell = GetCell(x, y);
        if (objCell == nullptr)
        {
            objCell = Create(x, y);
            if (objCell != nullptr)
            {
                objCell->Init(x, y, this);
            }
            else
            {
                sLogger.fatal("MapCell for x f% and y f% seems to be invalid!", x, y);
                return;
            }
        }

        // Build update-block for player
        ByteBuffer* buf = 0;
        uint32 count;
        Player* plObj = nullptr;

        if (obj->isPlayer())
        {
            plObj = static_cast<Player*>(obj);

            sLogger.debug("Creating player " I64FMT " for himself.", obj->getGuid());
            ByteBuffer pbuf(10000);
            count = plObj->buildCreateUpdateBlockForPlayer(&pbuf, plObj);
            plObj->getUpdateMgr().pushCreationData(&pbuf, count);
        }

        // Build in-range data
        uint8 cellNumber = worldConfig.server.mapCellNumber;

        uint32 endX = (x <= _sizeX) ? x + cellNumber : (_sizeX - cellNumber);
        uint32 endY = (y <= _sizeY) ? y + cellNumber : (_sizeY - cellNumber);
        uint32 startX = x > 0 ? x - cellNumber : 0;
        uint32 startY = y > 0 ? y - cellNumber : 0;

        for (uint32 posX = startX; posX <= endX; posX++)
        {
            for (uint32 posY = startY; posY <= endY; posY++)
            {
                MapCell* cell = GetCell(posX, posY);
                if (cell)
                {
                    UpdateInRangeSet(obj, plObj, cell, &buf);
                }
            }
        }

        // Forced Cells
        for (auto& cell : m_forcedcells)
            UpdateInRangeSet(obj, plObj, cell, &buf);

        //Add to the cell's object list
        objCell->AddObject(obj);

        obj->SetMapCell(objCell);
        //Add to the mapmanager's object list
        if (plObj != nullptr)
        {
            m_PlayerStorage[plObj->getGuidLow()] = plObj;
            UpdateCellActivity(x, y, 2 + cellNumber);
        }
        else
        {
            switch (obj->GetTypeFromGUID())
            {
            case HIGHGUID_TYPE_PET:
                m_PetStorage[obj->GetUIdFromGUID()] = static_cast<Pet*>(obj);
                break;

            case HIGHGUID_TYPE_UNIT:
            case HIGHGUID_TYPE_VEHICLE:
            {
                if (obj->GetUIdFromGUID() <= m_CreatureHighGuid)
                {
                    CreatureStorage[obj->GetUIdFromGUID()] = static_cast<Creature*>(obj);
                    if (static_cast<Creature*>(obj)->m_spawn != nullptr)
                    {
                        _sqlids_creatures.insert(std::make_pair(static_cast<Creature*>(obj)->m_spawn->id, static_cast<Creature*>(obj)));
                    }
                }
            }
            break;

            case HIGHGUID_TYPE_GAMEOBJECT:
            {
                GOStorage[obj->GetUIdFromGUID()] = static_cast<GameObject*>(obj);
                if (static_cast<GameObject*>(obj)->m_spawn != nullptr)
                {
                    _sqlids_gameobjects.insert(std::make_pair(static_cast<GameObject*>(obj)->m_spawn->id, static_cast<GameObject*>(obj)));
                }
            }
            break;

            case HIGHGUID_TYPE_DYNAMICOBJECT:
                m_DynamicObjectStorage[obj->getGuidLow()] = (DynamicObject*)obj;
                break;
            }
        }

        // Handle activation of that object.
        if (objCell->IsActive() && obj->CanActivate())
            obj->Activate(this);

        // Add the session to our set if it is a player.
        if (plObj)
        {
            Sessions.insert(plObj->GetSession());

            // Change the instance ID, this will cause it to be removed from the world thread (return value 1)
            plObj->GetSession()->SetInstance(GetInstanceID());

            // Add the map wide objects
            if (_mapWideStaticObjects.size())
            {
                uint32 globalcount = 0;
                if (!buf)
                    buf = new ByteBuffer(300);

                for (auto _mapWideStaticObject : _mapWideStaticObjects)
                {
                    count = _mapWideStaticObject->buildCreateUpdateBlockForPlayer(buf, plObj);
                    globalcount += count;
                }
                /*VLack: It seems if we use the same buffer then it is a BAD idea to try and push created data one by one, add them at once!
                       If you try to add them one by one, then as the buffer already contains data, they'll end up repeating some object.
                       Like 6 object updates for Deeprun Tram, but the built package will contain these entries: 2AFD0, 2AFD0, 2AFD1, 2AFD0, 2AFD1, 2AFD2*/
                if (globalcount > 0)
                    plObj->getUpdateMgr().pushCreationData(buf, globalcount);
            }
        }


        delete buf;

        if (plObj != nullptr && InactiveMoveTime && !forced_expire)
            InactiveMoveTime = 0;
    }
    else
    {
        sLogger.failure("MapMgr::PushObject tried to push invalid object (nullptr)!");
    }
}

void MapMgr::PushStaticObject(Object* obj)
{
    _mapWideStaticObjects.insert(obj);

    obj->SetInstanceID(GetInstanceID());
    obj->SetMapId(GetMapId());

    switch (obj->GetTypeFromGUID())
    {
        case HIGHGUID_TYPE_UNIT:
        case HIGHGUID_TYPE_VEHICLE:
            CreatureStorage[obj->GetUIdFromGUID()] = static_cast<Creature*>(obj);
            break;

        case HIGHGUID_TYPE_GAMEOBJECT:
            GOStorage[obj->GetUIdFromGUID()] = static_cast<GameObject*>(obj);
            break;

        default:
            sLogger.debug("MapMgr::PushStaticObject called for invalid type %u.", obj->GetTypeFromGUID());
            break;
    }
}

void MapMgr::RemoveObject(Object* obj, bool free_guid)
{
    // Assertions
    if (obj == nullptr)
    {
        sLogger.failure("MapMgr::RemoveObject tried to remove invalid object (nullptr)");
        return;
    }

    if (obj->GetMapId() != _mapId)
    {
        sLogger.failure("MapMgr::RemoveObject tried to remove object with map %u but mapMgr is for map %u!", obj->GetMapId(), _mapId);
        return;
    }

    if (_cells == nullptr)
    {
        sLogger.failure("MapMgr::RemoveObject tried to remove invalid cells (nullptr)");
        return;
    }

    if (obj->IsActive())
        obj->Deactivate(this);

    //there is a very small chance that on double player ports on same update player is added to multiple insertpools but not removed
    //one clear example was the double port proc when exploiting double resurrect
    m_objectinsertlock.Acquire();
    m_objectinsertpool.erase(obj);
    m_objectinsertlock.Release();

    _updates.erase(obj);
    obj->ClearUpdateMask();

    // Remove object from all needed places
    switch (obj->GetTypeFromGUID())
    {
        case HIGHGUID_TYPE_UNIT:
        case HIGHGUID_TYPE_VEHICLE:
        {
            if (obj->GetUIdFromGUID() <= m_CreatureHighGuid)
            {
                CreatureStorage[obj->GetUIdFromGUID()] = nullptr;

                if (static_cast<Creature*>(obj)->m_spawn != nullptr)
                    _sqlids_creatures.erase(static_cast<Creature*>(obj)->m_spawn->id);

                if (free_guid)
                    _reusable_guids_creature.push_back(obj->GetUIdFromGUID());
            }
            break;
        }
        case HIGHGUID_TYPE_PET:
        {
            if (pet_iterator != m_PetStorage.end() && pet_iterator->second->getGuid() == obj->getGuid())
                ++pet_iterator;
            m_PetStorage.erase(obj->GetUIdFromGUID());

            break;
        }
        case HIGHGUID_TYPE_DYNAMICOBJECT:
        {
            m_DynamicObjectStorage.erase(obj->getGuidLow());

            break;
        }
        case HIGHGUID_TYPE_GAMEOBJECT:
        {
            if (obj->GetUIdFromGUID() <= m_GOHighGuid)
            {
                GOStorage[obj->GetUIdFromGUID()] = nullptr;
                if (static_cast<GameObject*>(obj)->m_spawn != nullptr)
                    _sqlids_gameobjects.erase(static_cast<GameObject*>(obj)->m_spawn->id);

                if (free_guid)
                    _reusable_guids_gameobject.push_back(obj->GetUIdFromGUID());
            }
            break;
        }
        case HIGHGUID_TYPE_TRANSPORTER:
        {
            break;
        }
        default:
        {
            sLogger.debug("MapMgr::RemoveObject called for invalid type %u.", obj->GetTypeFromGUID());
            break;
        }
    }

    //\todo That object types are not map objects. TODO: add AI groups here?
    if (obj->isItem() || obj->isContainer())
    {
        return;
    }

    if (obj->isCorpse())
    {
        m_corpses.erase(static_cast< Corpse* >(obj));
    }

    MapCell* cell = GetCell(obj->GetMapCellX(), obj->GetMapCellY());
    if (cell == nullptr)
    {
        // set the map cell correctly
        if (obj->GetPositionX() < _maxX || obj->GetPositionX() > _minY || obj->GetPositionY() < _maxY || obj->GetPositionY() > _minY)
        {
            cell = this->GetCellByCoords(obj->GetPositionX(), obj->GetPositionY());
            obj->SetMapCell(cell);
        }
    }

    if (cell != nullptr)
    {
        cell->RemoveObject(obj);        // Remove object from cell
        obj->SetMapCell(nullptr);          // Unset object's cell
    }

    Player* plObj = nullptr;
    if (obj->isPlayer())
    {
        plObj = static_cast<Player*>(obj);

        _processQueue.erase(plObj);     // Clear any updates pending
        plObj->getUpdateMgr().clearPendingUpdates();
    }

    obj->removeSelfFromInrangeSets();
    obj->clearInRangeSets();             // Clear object's in-range set

    uint8 cellNumber = worldConfig.server.mapCellNumber;

    // If it's a player - update his nearby cells
    if (!_shutdown)
    {
        if (obj->isPlayer())
        {// get x/y
            if (obj->GetPositionX() < _maxX || obj->GetPositionX() > _minY || obj->GetPositionY() < _maxY || obj->GetPositionY() > _minY)
            {
                uint32 x = GetPosX(obj->GetPositionX());
                uint32 y = GetPosY(obj->GetPositionY());
                UpdateCellActivity(x, y, 2 + cellNumber);
            }
            m_PlayerStorage.erase(obj->getGuidLow());
        }
        else if (obj->isCreatureOrPlayer() && static_cast<Unit*>(obj)->mPlayerControler != nullptr)
        {
            if (obj->GetPositionX() < _maxX || obj->GetPositionX() > _minY || obj->GetPositionY() < _maxY || obj->GetPositionY() > _minY)
            {
                uint32 x = GetPosX(obj->GetPositionX());
                uint32 y = GetPosY(obj->GetPositionY());
                UpdateCellActivity(x, y, 2 + cellNumber);
            }
        }
    }

    // Remove the session from our set if it is a player.
    if (obj->isPlayer() || obj->isCreatureOrPlayer() && static_cast<Unit*>(obj)->mPlayerControler != nullptr)
    {
        for (auto _mapWideStaticObject : _mapWideStaticObjects)
        {
            if (_mapWideStaticObject != nullptr && plObj)
                plObj->getUpdateMgr().pushOutOfRangeGuid(_mapWideStaticObject->GetNewGUID());
        }

        // Setting an instance ID here will trigger the session to be removed by MapMgr::run(). :)
        if (plObj && plObj->GetSession())
        {
            plObj->GetSession()->SetInstance(0);

            // Add it to the global session set. Don't "re-add" to session if it is being deleted.
            if (!plObj->GetSession()->bDeleted)
                sWorld.addGlobalSession(plObj->GetSession());
        }
    }

    if (!HasPlayers())
    {
        if (this->pInstance != nullptr && this->pInstance->m_persistent)
            this->pInstance->m_creatorGroup = 0;

        if (!InactiveMoveTime && !forced_expire && !GetMapInfo()->isNonInstanceMap())
        {
            InactiveMoveTime = UNIXTIME + (30 * 60); //mapmgr inactive move time 30
            sLogger.debug("MapMgr : Instance %u is now idle. (%s)", m_instanceID, GetBaseMap()->GetMapName().c_str());
        }
    }
}

void MapMgr::ChangeObjectLocation(Object* obj)
{
    if (obj == nullptr)
        return;

    // Items and containers are of no interest for us
    if (obj->isItem() || obj->isContainer() || obj->GetMapMgr() != this)
        return;

    Player* plObj = nullptr;
    ByteBuffer* buf = nullptr;

    if (obj->isPlayer())
        plObj = static_cast<Player*>(obj);
    if (obj->isCreatureOrPlayer() && static_cast<Unit*>(obj)->mPlayerControler != nullptr)
        plObj = static_cast<Unit*>(obj)->mPlayerControler;

    float fRange = 0.0f;

    // Update in-range data for old objects
    if (obj->hasInRangeObjects())
    {
        for (const auto& iter : obj->getInRangeObjectsSet())
        {
            if (iter)
            {
                Object* curObj = iter;

                fRange = GetUpdateDistance(curObj, obj, plObj);

                if (fRange > 0.0f && (curObj->GetDistance2dSq(obj) > fRange))
                {
                    if (plObj != nullptr)
                        plObj->RemoveIfVisible(curObj->getGuid());

                    if (curObj->isPlayer())
                        static_cast<Player*>(curObj)->RemoveIfVisible(obj->getGuid());

                    if (curObj->isCreatureOrPlayer() && static_cast<Unit*>(curObj)->mPlayerControler != nullptr)
                        static_cast<Unit*>(curObj)->mPlayerControler->RemoveIfVisible(obj->getGuid());

                    curObj->removeObjectFromInRangeObjectsSet(obj);

                    if (obj->GetMapMgr() != this)
                        return;             //Something removed us.

                    obj->removeObjectFromInRangeObjectsSet(curObj);
                }
            }
        }
    }

    // Get new cell coordinates
    if (obj->GetMapMgr() != this)
    {
        return;                 //Something removed us.
    }

    if (obj->GetPositionX() >= _maxX || obj->GetPositionX() <= _minX || obj->GetPositionY() >= _maxY || obj->GetPositionY() <= _minY)
    {
        OutOfMapBoundariesTeleport(obj);
    }

    uint32 cellX = GetPosX(obj->GetPositionX());
    uint32 cellY = GetPosY(obj->GetPositionY());

    if (cellX >= _sizeX || cellY >= _sizeY)
    {
        return;
    }

    MapCell* objCell = GetCell(cellX, cellY);
    MapCell* pOldCell = obj->GetMapCell();
    if (objCell == nullptr)
    {
        objCell = Create(cellX, cellY);
        if (objCell != nullptr)
        {
            objCell->Init(cellX, cellY, this);
        }
        else
        {
            sLogger.failure("MapMgr::ChangeObjectLocation not able to create object cell (nullptr), return!");
            return;
        }
    }
    uint8 cellNumber = worldConfig.server.mapCellNumber;

    // If object moved cell
    if (objCell != pOldCell)
    {
        // THIS IS A HACK!
        // Current code, if a creature on a long waypoint path moves from an active
        // cell into an inactive one, it will disable itself and will never return.
        // This is to prevent cpu leaks. I will think of a better solution very soon :P

        if (!objCell->IsActive() && !plObj && obj->IsActive())
            obj->Deactivate(this);

        if (pOldCell != nullptr)
            pOldCell->RemoveObject(obj);

        objCell->AddObject(obj);
        obj->SetMapCell(objCell);

        // if player we need to update cell activity radius = 2 is used in order to update
        // both old and new cells
        if (obj->isPlayer() || obj->isCreatureOrPlayer() && static_cast<Unit*>(obj)->mPlayerControler != nullptr)
        {
            // have to unlock/lock here to avoid a deadlock situation.
            UpdateCellActivity(cellX, cellY, 2 + cellNumber);
            if (pOldCell != NULL)
            {
                // only do the second check if there's -/+ 2 difference
                if (abs((int)cellX - (int)pOldCell->_x) > 2 + cellNumber ||
                    abs((int)cellY - (int)pOldCell->_y) > 2 + cellNumber)
                {
                    UpdateCellActivity(pOldCell->_x, pOldCell->_y, cellNumber);
                }
            }
        }
    }

    // Update in-range set for new objects
    uint32 endX = cellX + cellNumber;
    uint32 endY = cellY + cellNumber;
    uint32 startX = cellX > 0 ? cellX - cellNumber : 0;
    uint32 startY = cellY > 0 ? cellY - cellNumber : 0;

    //If the object announcing it's position is a special one, then it should do so in a much wider area - like the distance between the two transport towers in Orgrimmar, or more. - By: VLack
    if (obj->isGameObject() && (static_cast< GameObject* >(obj)->GetOverrides() & GAMEOBJECT_ONMOVEWIDE))
    {
        endX = cellX + 5 <= _sizeX ? cellX + 6 : (_sizeX - 1);
        endY = cellY + 5 <= _sizeY ? cellY + 6 : (_sizeY - 1);
        startX = cellX > 5 ? cellX - 6 : 0;
        startY = cellY > 5 ? cellY - 6 : 0;
    }

    for (uint32 posX = startX; posX <= endX; ++posX)
    {
        for (uint32 posY = startY; posY <= endY; ++posY)
        {
            MapCell* cell = GetCell(posX, posY);
            if (cell)
                UpdateInRangeSet(obj, plObj, cell, &buf);
        }
    }

    if (buf)
        delete buf;
}

void MapMgr::OutOfMapBoundariesTeleport(Object* object)
{
    if (object->isPlayer())
    {
        Player* player = static_cast<Player*>(object);

        if (player->getBindMapId() != GetMapId())
        {
            player->SafeTeleport(player->getBindMapId(), 0, player->getBindPosition());
            player->GetSession()->SystemMessage("Teleported you to your hearthstone location as you were out of the map boundaries.");
        }
        else
        {
            object->GetPositionV()->ChangeCoords(player->getBindPosition());
            player->GetSession()->SystemMessage("Teleported you to your hearthstone location as you were out of the map boundaries.");
            player->SendTeleportAckPacket(player->getBindPosition().x, player->getBindPosition().y, player->getBindPosition().z, 0);
        }
    }
    else
    {
        object->GetPositionV()->ChangeCoords({ 0, 0, 0, 0 });
    }
}

void MapMgr::UpdateInRangeSet(Object* obj, Player* plObj, MapCell* cell, ByteBuffer** buf)
{
    if (cell == nullptr)
        return;

    Player* plObj2;
    int count;
    bool cansee, isvisible;

    auto iter = cell->Begin();
    while (iter != cell->End())
    {
        // Prevent undefined behaviour (related to transports) -Appled
        if (cell->_objects.empty())
            break;

        Object* curObj = *iter;
        ++iter;

        if (curObj == nullptr)
            continue;

        float fRange = GetUpdateDistance(curObj, obj, plObj);

        if (curObj != obj && (curObj->GetDistance2dSq(obj) <= fRange || fRange == 0.0f))
        {
            if (!obj->isObjectInInRangeObjectsSet(curObj))
            {
                obj->addToInRangeObjects(curObj);          // Object in range, add to set
                curObj->addToInRangeObjects(obj);

                if (curObj->isPlayer())
                {
                    plObj2 = static_cast<Player*>(curObj);

                    if (plObj2->canSee(obj) && !plObj2->IsVisible(obj->getGuid()))
                    {
                        if (!*buf)
                            * buf = new ByteBuffer(2500);

                        count = obj->buildCreateUpdateBlockForPlayer(*buf, plObj2);
                        plObj2->getUpdateMgr().pushCreationData(*buf, count);
                        plObj2->AddVisibleObject(obj->getGuid());
                        (*buf)->clear();
                    }
                }
                else if (curObj->isCreatureOrPlayer() && static_cast<Unit*>(curObj)->mPlayerControler != nullptr)
                {
                    plObj2 = static_cast<Unit*>(curObj)->mPlayerControler;

                    if (plObj2->canSee(obj) && !plObj2->IsVisible(obj->getGuid()))
                    {
                        if (!*buf)
                            * buf = new ByteBuffer(2500);

                        count = obj->buildCreateUpdateBlockForPlayer(*buf, plObj2);
                        plObj2->getUpdateMgr().pushCreationData(*buf, count);
                        plObj2->AddVisibleObject(obj->getGuid());
                        (*buf)->clear();
                    }
                }

                if (plObj != nullptr)
                {
                    if (plObj->canSee(curObj) && !plObj->IsVisible(curObj->getGuid()))
                    {
                        if (!*buf)
                            * buf = new ByteBuffer(2500);

                        count = curObj->buildCreateUpdateBlockForPlayer(*buf, plObj);
                        plObj->getUpdateMgr().pushCreationData(*buf, count);
                        plObj->AddVisibleObject(curObj->getGuid());
                        (*buf)->clear();
                    }
                }
            }
            else
            {
                // Check visibility
                if (curObj->isPlayer())
                {
                    plObj2 = static_cast<Player*>(curObj);
                    cansee = plObj2->canSee(obj);
                    isvisible = plObj2->IsVisible(obj->getGuid());
                    if (!cansee && isvisible)
                    {
                        plObj2->getUpdateMgr().pushOutOfRangeGuid(obj->GetNewGUID());
                        plObj2->RemoveVisibleObject(obj->getGuid());
                    }
                    else if (cansee && !isvisible)
                    {
                        if (!*buf)
                            * buf = new ByteBuffer(2500);

                        count = obj->buildCreateUpdateBlockForPlayer(*buf, plObj2);
                        plObj2->getUpdateMgr().pushCreationData(*buf, count);
                        plObj2->AddVisibleObject(obj->getGuid());
                        (*buf)->clear();
                    }
                }
                else if (curObj->isCreatureOrPlayer() && static_cast<Unit*>(curObj)->mPlayerControler != nullptr)
                {
                    plObj2 = static_cast<Unit*>(curObj)->mPlayerControler;
                    cansee = plObj2->canSee(obj);
                    isvisible = plObj2->IsVisible(obj->getGuid());
                    if (!cansee && isvisible)
                    {
                        plObj2->getUpdateMgr().pushOutOfRangeGuid(obj->GetNewGUID());
                        plObj2->RemoveVisibleObject(obj->getGuid());
                    }
                    else if (cansee && !isvisible)
                    {
                        if (!*buf)
                            * buf = new ByteBuffer(2500);

                        count = obj->buildCreateUpdateBlockForPlayer(*buf, plObj2);
                        plObj2->getUpdateMgr().pushCreationData(*buf, count);
                        plObj2->AddVisibleObject(obj->getGuid());
                        (*buf)->clear();
                    }
                }

                if (plObj != nullptr)
                {
                    cansee = plObj->canSee(curObj);
                    isvisible = plObj->IsVisible(curObj->getGuid());
                    if (!cansee && isvisible)
                    {
                        plObj->getUpdateMgr().pushOutOfRangeGuid(curObj->GetNewGUID());
                        plObj->RemoveVisibleObject(curObj->getGuid());
                    }
                    else if (cansee && !isvisible)
                    {
                        if (!*buf)
                            * buf = new ByteBuffer(2500);

                        count = curObj->buildCreateUpdateBlockForPlayer(*buf, plObj);
                        plObj->getUpdateMgr().pushCreationData(*buf, count);
                        plObj->AddVisibleObject(curObj->getGuid());
                        (*buf)->clear();
                    }
                }
            }
        }
    }
}

float MapMgr::GetUpdateDistance(Object* curObj, Object* obj, Player* plObj)
{
    static float no_distance = 0.0f;

    // had a crash because this was nullptr just send default update distance
    if (!plObj)
        return m_UpdateDistance;

    // unlimited distance for people on same boat
    if (curObj->isPlayer() && obj->isPlayer() && plObj != nullptr && plObj->obj_movement_info.hasMovementFlag(MOVEFLAG_TRANSPORT) && plObj->obj_movement_info.transport_guid == curObj->obj_movement_info.transport_guid)
        return no_distance;
    // unlimited distance for transporters (only up to 2 cells +/- anyway.)
    if (curObj->GetTypeFromGUID() == HIGHGUID_TYPE_TRANSPORTER)
        return no_distance;

    // unlimited distance in Instances/Raids
    if (GetMapInfo()->isInstanceMap())
        return no_distance;

    //If the object announcing its position is a transport, or other special object, then deleting it from visible objects should be avoided. - By: VLack
    if (obj->isGameObject() && (static_cast<GameObject*>(obj)->GetOverrides() & GAMEOBJECT_INFVIS) && obj->GetMapId() == curObj->GetMapId())
        return no_distance;

    //If the object we're checking for possible removal is a transport or other special object, and we are players on the same map, don't remove it, and add it whenever possible...
    if (plObj && curObj->isGameObject() && (static_cast<GameObject*>(curObj)->GetOverrides() & GAMEOBJECT_INFVIS) && obj->GetMapId() == curObj->GetMapId())
        return no_distance;

    // normal distance
    return m_UpdateDistance;
}

void MapMgr::_UpdateObjects()
{
    if (!_updates.size() && !_processQueue.size())
        return;

    ByteBuffer update(2500);
    uint32 count = 0;

    m_updateMutex.Acquire();

    for (auto pObj : _updates)
    {
        if (pObj == nullptr)
            continue;

        if (pObj->isItem() || pObj->isContainer())
        {
            // our update is only sent to the owner here.
            Player* pOwner = static_cast<Item*>(pObj)->getOwner();
            if (pOwner != nullptr)
            {
                count = pObj->BuildValuesUpdateBlockForPlayer(&update, pOwner);
                // send update to owner
                if (count)
                {
                    pOwner->getUpdateMgr().pushUpdateData(&update, count);
                    update.clear();
                }
            }
        }
        else
        {
            if (pObj->IsInWorld())
            {
                // players have to receive their own updates ;)
                if (pObj->isPlayer())
                {
                    // need to be different! ;)
                    count = pObj->BuildValuesUpdateBlockForPlayer(&update, static_cast<Player*>(pObj));
                    if (count)
                    {
                        static_cast<Player*>(pObj)->getUpdateMgr().pushUpdateData(&update, count);
                        update.clear();
                    }
                }
                else if (pObj->isCreatureOrPlayer() && static_cast<Unit*>(pObj)->mPlayerControler != nullptr)
                {
                    count = pObj->BuildValuesUpdateBlockForPlayer(&update, static_cast<Unit*>(pObj)->mPlayerControler);
                    if (count)
                    {
                        static_cast<Unit*>(pObj)->mPlayerControler->getUpdateMgr().pushUpdateData(&update, count);
                        update.clear();
                    }
                }

                // build the update
                count = pObj->BuildValuesUpdateBlockForPlayer(&update, static_cast<Player*>(NULL));

                if (count)
                {
                    for (const auto& itr : pObj->getInRangePlayersSet())
                    {
                        Player* lplr = static_cast<Player*>(itr);

                        // Make sure that the target player can see us.
                        if (lplr && lplr->IsVisible(pObj->getGuid()))
                            lplr->getUpdateMgr().pushUpdateData(&update, count);
                    }
                    update.clear();
                }
            }
        }
        pObj->ClearUpdateMask();
    }
    _updates.clear();
    m_updateMutex.Release();

    // generate pending a9packets and send to clients.
    for (auto it = _processQueue.begin(); it != _processQueue.end();)
    {
        Player* player = *it;

        auto it2 = it;
        ++it;

        _processQueue.erase(it2);
        if (player->GetMapMgr() == this)
            player->ProcessPendingUpdates();
    }
}


uint32 MapMgr::GetPlayerCount()
{
    return static_cast<uint32>(m_PlayerStorage.size());
}

void MapMgr::respawnBossLinkedGroups(uint32_t bossId)
{
    // Get all Killed npcs out of Killed npc Store
    for (Creature* spawn : sMySQLStore.getSpawnGroupDataByBoss(bossId))
    {
        if (spawn && spawn->m_spawn && spawn->getSpawnId())
        {
            // Get the Group Data and see if we have to Respawn the npcs
            auto data = sMySQLStore.getSpawnGroupDataBySpawn(spawn->getSpawnId());

            if (data && data->spawnFlags & SPAWFLAG_FLAG_BOUNDTOBOSS)
            {
                // Respawn the Npc
                if (spawn->IsInWorld() && !spawn->isAlive())
                {
                    spawn->Despawn(0, 1000);
                }
                else if (!spawn->isAlive())
                {
                    // get the cell with our SPAWN location. if we've moved cell this might break :P
                    MapCell* pCell = GetCellByCoords(spawn->GetSpawnX(), spawn->GetSpawnY());
                    if (pCell == nullptr)
                        pCell = spawn->GetMapCell();

                    if (pCell != nullptr)
                    {
                        pCell->_respawnObjects.insert(spawn);

                        sEventMgr.RemoveEvents(spawn);
                        EventRespawnCreature(spawn, pCell->GetPositionX(), pCell->GetPositionY());

                        spawn->SetPosition(spawn->GetSpawnPosition(), true);
                        spawn->m_respawnCell = pCell;
                    }
                }
            }

            // Erease from Killed Npcs
            pInstance->m_killedNpcs.erase(spawn->getSpawnId());
        }
    }

    // Save the Instance
    sInstanceMgr.SaveInstanceToDB(pInstance);
}

void MapMgr::spawnManualGroup(uint32_t groupId)
{
    auto data = sMySQLStore.getSpawnGroupDataByGroup(groupId);

    if (data)
    {
        for (auto spawns : data->spawns)
        {
            if (auto creature = spawns.second)
            {
                creature->PrepareForRemove();

                // get the cell with our SPAWN location. if we've moved cell this might break :P
                MapCell* pCell = GetCellByCoords(creature->GetSpawnX(), creature->GetSpawnY());
                if (pCell == nullptr)
                    pCell = creature->GetMapCell();

                if (pCell != nullptr)
                {
                    pCell->_respawnObjects.insert(creature);

                    sEventMgr.RemoveEvents(creature);
                    EventRespawnCreature(creature, pCell->GetPositionX(), pCell->GetPositionY());

                    creature->SetPosition(creature->GetSpawnPosition(), true);
                    creature->m_respawnCell = pCell;
                }
            }
        }
    }
}

void MapMgr::UpdateCellActivity(uint32 x, uint32 y, uint32 radius)
{
    CellSpawns* sp;
    uint32 endX = (x + radius) <= _sizeX ? x + radius : (_sizeX - 1);
    uint32 endY = (y + radius) <= _sizeY ? y + radius : (_sizeY - 1);
    uint32 startX = x > radius ? x - radius : 0;
    uint32 startY = y > radius ? y - radius : 0;

    for (uint32 posX = startX; posX <= endX; posX++)
    {
        for (uint32 posY = startY; posY <= endY; posY++)
        {
            MapCell* objCell = GetCell(posX, posY);
            if (objCell == nullptr)
            {
                if (_CellActive(posX, posY))
                {
                    objCell = Create(posX, posY);
                    objCell->Init(posX, posY, this);

                    sLogger.debug("MapMgr : Cell [%u,%u] on map %u (instance %u) is now active.", posX, posY, this->_mapId, m_instanceID);
                    objCell->SetActivity(true);

                    _terrain->LoadTile((int32)posX / 8, (int32)posY / 8);

                    if (!objCell->IsLoaded())
                    {
                        sLogger.debug("MapMgr : Loading objects for Cell [%u][%u] on map %u (instance %u)...", posX, posY, this->_mapId, m_instanceID);

                        sp = _map->GetSpawnsList(posX, posY);
                        if (sp)
                            objCell->LoadObjects(sp);
                    }
                }
            }
            else
            {
                //Cell is now active
                if (_CellActive(posX, posY) && !objCell->IsActive())
                {
                    sLogger.debug("Cell [%u,%u] on map %u (instance %u) is now active.", posX, posY, this->_mapId, m_instanceID);

                    _terrain->LoadTile((int32)posX / 8, (int32)posY / 8);
                    objCell->SetActivity(true);

                    if (!objCell->IsLoaded())
                    {
                        sLogger.debug("Loading objects for Cell [%u][%u] on map %u (instance %u)...", posX, posY, this->_mapId, m_instanceID);
                        sp = _map->GetSpawnsList(posX, posY);
                        if (sp)
                            objCell->LoadObjects(sp);
                    }
                }
                //Cell is no longer active
                else if (!_CellActive(posX, posY) && objCell->IsActive())
                {
                    sLogger.debug("Cell [%u,%u] on map %u (instance %u) is now idle.", posX, posY, _mapId, m_instanceID);
                    objCell->SetActivity(false);

                    _terrain->UnloadTile((int32)posX / 8, (int32)posY / 8);
                }
            }
        }
    }
}

float MapMgr::GetLandHeight(float x, float y, float z)
{
    if (worldConfig.terrainCollision.isCollisionEnabled)
    {
        float adtheight = GetADTLandHeight(x, y);

        VMAP::IVMapManager* vmgr = VMAP::VMapFactory::createOrGetVMapManager();
        float vmapheight = vmgr->getHeight(_mapId, x, y, z + 0.5f, 10000.0f);

        if (adtheight > z && vmapheight > -1000)
            return vmapheight; //underground

        return std::max(vmapheight, adtheight);
    }
    else
        return z;
}

float MapMgr::getWaterOrGroundLevel(uint32 phasemask, float x, float y, float z, float* ground /*= nullptr*/, bool /*swim = false*/, float collisionHeight /*= DEFAULT_COLLISION_HEIGHT*/)
{
    if (TerrainTile* tile = _terrain->GetTile(x, y))
    {
        // we need ground level (including grid height version) for proper return water level in point
        float ground_z = GetLandHeight(x, y, z + collisionHeight);
        if (ground)
            *ground = ground_z;

        LiquidData liquid_status;

        ZLiquidStatus res = getLiquidStatus(phasemask, x, y, ground_z, MAP_ALL_LIQUIDS, &liquid_status, collisionHeight);
        switch (res)
        {
        case LIQUID_MAP_ABOVE_WATER:
            return std::max<float>(liquid_status.level, ground_z);
        case LIQUID_MAP_NO_WATER:
            return ground_z;
        default:
            return liquid_status.level;
        }
    }

    return VMAP_INVALID_HEIGHT_VALUE;
}

bool MapMgr::isUnderWater(float x, float y, float z)
{
    float watermark = GetLiquidHeight(x, y);

    if (watermark > (z + 0.5f))
        return true;

    return false;
}

float MapMgr::GetADTLandHeight(float x, float y)
{
    TerrainTile* tile = _terrain->GetTile(x, y);
    if (tile == nullptr)
        return TERRAIN_INVALID_HEIGHT;

    float rv = tile->m_map.GetHeight(x, y);
    tile->DecRef();

    return rv;
}

bool MapMgr::IsUnderground(float x, float y, float z)
{
    return GetADTLandHeight(x, y) > (z + 0.5f);
}

bool MapMgr::GetLiquidInfo(float x, float y, float z, float& liquidlevel, uint32& liquidtype)
{
    VMAP::IVMapManager* vmgr = VMAP::VMapFactory::createOrGetVMapManager();

    float flr;
    if (vmgr->GetLiquidLevel(_mapId, x, y, z, 0xFF, liquidlevel, flr, liquidtype))
        return true;

    liquidlevel = GetLiquidHeight(x, y);
    liquidtype = GetLiquidType(x, y);

    if (liquidtype == 0)
        return false;

    return true;
}

float MapMgr::GetLiquidHeight(float x, float y)
{
    TerrainTile* tile = _terrain->GetTile(x, y);
    if (tile == nullptr)
        return TERRAIN_INVALID_HEIGHT;

    float rv = tile->m_map.GetTileLiquidHeight(x, y);
    tile->DecRef();

    return rv;
}

uint8 MapMgr::GetLiquidType(float x, float y)
{
    TerrainTile* tile = _terrain->GetTile(x, y);
    if (tile == nullptr)
        return 0;

    uint8 rv = tile->m_map.GetTileLiquidType(x, y);
    tile->DecRef();

    return rv;
}

static inline bool isInWMOInterior(uint32 mogpFlags)
{
    return (mogpFlags & 0x2000) != 0;
}

ZLiquidStatus MapMgr::getLiquidStatus(uint32 /*phaseMask*/, float x, float y, float z, uint8 ReqLiquidType, LiquidData* data, float collisionHeight)
{
    ZLiquidStatus result = LIQUID_MAP_NO_WATER;
    VMAP::IVMapManager* vmgr = VMAP::VMapFactory::createOrGetVMapManager();
    float liquid_level = INVALID_HEIGHT;
    float ground_level = INVALID_HEIGHT;
    uint32 liquid_type = 0;
    uint32 mogpFlags = 0;
    bool useGridLiquid = true;
    if (vmgr->GetLiquidLevel(_mapId, x, y, z, ReqLiquidType, liquid_level, ground_level, liquid_type))
    {
        useGridLiquid = !isInWMOInterior(mogpFlags);
        // Check water level and ground level
        if (liquid_level > ground_level && G3D::fuzzyGe(z, ground_level - GROUND_HEIGHT_TOLERANCE))
        {
            // All ok in water -> store data
            if (data)
            {
                // hardcoded in client like this
                if (_mapId == 530 && liquid_type == 2)
                    liquid_type = 15;

                uint32 liquidFlagType = 0;
                if (::DBC::Structures::LiquidTypeEntry const* liq = sLiquidTypeStore.LookupEntry(liquid_type))
                    liquidFlagType = liq->Type;

                if (liquid_type && liquid_type < 21)
                {
                    if (auto const* area = GetArea(x, y, z))
                    {
#if VERSION_STRING > Classic
                        uint32 overrideLiquid = area->liquid_type_override[liquidFlagType];
                        if (!overrideLiquid && area->zone)
                        {
                            area = MapManagement::AreaManagement::AreaStorage::GetAreaById(area->zone);
                            if (area)
                                overrideLiquid = area->liquid_type_override[liquidFlagType];
                        }
#else
                        uint32 overrideLiquid = area->liquid_type_override;
                        if (!overrideLiquid && area->zone)
                        {
                            area = MapManagement::AreaManagement::AreaStorage::GetAreaById(area->zone);
                            if (area)
                                overrideLiquid = area->liquid_type_override;
                        }
#endif

                        if (::DBC::Structures::LiquidTypeEntry const* liq = sLiquidTypeStore.LookupEntry(overrideLiquid))
                        {
                            liquid_type = overrideLiquid;
                            liquidFlagType = liq->Type;
                        }
                    }
                }

                data->level = liquid_level;
                data->depth_level = ground_level;

                data->entry = liquid_type;
                data->type_flags = 1 << liquidFlagType;
            }

            float delta = liquid_level - z;

            // Get position delta
            if (delta > collisionHeight)        // Under water
                return LIQUID_MAP_UNDER_WATER;
            if (delta > 0.0f)                   // In water
                return LIQUID_MAP_IN_WATER;
            if (delta > -0.1f)                  // Walk on water
                return LIQUID_MAP_WATER_WALK;
            result = LIQUID_MAP_ABOVE_WATER;
        }
    }

    /*if (useGridLiquid)
    {
        if (GridMap* gmap = const_cast<Map*>(this)->GetGrid(x, y))
        {
            LiquidData map_data;
            ZLiquidStatus map_result = gmap->getLiquidStatus(x, y, z, ReqLiquidType, &map_data, collisionHeight);
            // Not override LIQUID_MAP_ABOVE_WATER with LIQUID_MAP_NO_WATER:
            if (map_result != LIQUID_MAP_NO_WATER && (map_data.level > ground_level))
            {
                if (data)
                {
                    // hardcoded in client like this
                    if (_mapId == 530 && map_data.entry == 2)
                        map_data.entry = 15;

                    *data = map_data;
                }
                return map_result;
            }
        }
    }*/
    return result;
}

const ::DBC::Structures::AreaTableEntry* MapMgr::GetArea(float x, float y, float z)
{
    uint32 mogp_flags;
    int32 adt_id;
    int32 root_id;
    int32 group_id;

    bool have_area_info = _terrain->GetAreaInfo(x, y, z, mogp_flags, adt_id, root_id, group_id);
    auto area_flag_without_adt_id = _terrain->GetAreaFlagWithoutAdtId(x, y);
    auto area_flag = MapManagement::AreaManagement::AreaStorage::GetFlagByPosition(area_flag_without_adt_id, have_area_info, mogp_flags, adt_id, root_id, group_id, _mapId, x, y, z, nullptr);

    if (area_flag)
        return MapManagement::AreaManagement::AreaStorage::GetAreaByFlag(area_flag);

    return MapManagement::AreaManagement::AreaStorage::GetAreaByMapId(_mapId);
}

bool MapMgr::isInLineOfSight(float x, float y, float z, float x2, float y2, float z2)
{
    VMAP::IVMapManager* vmgr = VMAP::VMapFactory::createOrGetVMapManager();

    return vmgr->isInLineOfSight(GetMapId(), x, y, z, x2, y2, z2);
}

uint32 MapMgr::GetMapId()
{
    return _mapId;
}

bool MapMgr::_CellActive(uint32 x, uint32 y)
{
    uint8 cellNumber = worldConfig.server.mapCellNumber;

    uint32 endX = ((x + cellNumber) <= _sizeX) ? x + cellNumber : (_sizeX - cellNumber);
    uint32 endY = ((y + cellNumber) <= _sizeY) ? y + cellNumber : (_sizeY - cellNumber);
    uint32 startX = x > 0 ? x - cellNumber : 0;
    uint32 startY = y > 0 ? y - cellNumber : 0;

    for (uint32 posX = startX; posX <= endX; posX++)
    {
        for (uint32 posY = startY; posY <= endY; posY++)
        {
            MapCell* objCell = GetCell(posX, posY);
            if (objCell != nullptr)
            {
                if (objCell->HasPlayers() || m_forcedcells.find(objCell) != m_forcedcells.end())
                {
                    return true;
                }
            }
        }
    }

    return false;
}

void MapMgr::ObjectUpdated(Object* obj)
{
    // set our fields to dirty stupid fucked up code in places.. i hate doing this but i've got to :<- burlex
    m_updateMutex.Acquire();
    _updates.insert(obj);
    m_updateMutex.Release();
}

void MapMgr::PushToProcessed(Player* plr)
{
    _processQueue.insert(plr);
}

bool MapMgr::HasPlayers()
{
    return (m_PlayerStorage.size() > 0);
}

bool MapMgr::IsCombatInProgress()
{
    return (_combatProgress.size() > 0);
}

void MapMgr::ChangeFarsightLocation(Player* plr, DynamicObject* farsight)
{
    uint8 cellNumber = worldConfig.server.mapCellNumber;

    if (farsight == 0)
    {
        // We're clearing.
        for (auto itr = plr->m_visibleFarsightObjects.begin(); itr != plr->m_visibleFarsightObjects.end(); ++itr)
        {
            if (plr->IsVisible((*itr)->getGuid()) && !plr->canSee((*itr)))
                plr->getUpdateMgr().pushOutOfRangeGuid((*itr)->GetNewGUID());      // Send destroy
        }

        plr->m_visibleFarsightObjects.clear();
    }
    else
    {
        uint32 cellX = GetPosX(farsight->GetPositionX());
        uint32 cellY = GetPosY(farsight->GetPositionY());
        uint32 endX = (cellX <= _sizeX) ? cellX + cellNumber : (_sizeX - cellNumber);
        uint32 endY = (cellY <= _sizeY) ? cellY + cellNumber : (_sizeY - cellNumber);
        uint32 startX = cellX > 0 ? cellX - cellNumber : 0;
        uint32 startY = cellY > 0 ? cellY - cellNumber : 0;

        for (uint32 posX = startX; posX <= endX; ++posX)
        {
            for (uint32 posY = startY; posY <= endY; ++posY)
            {
                MapCell* cell = GetCell(posX, posY);
                if (cell != nullptr)
                {
                    for (auto iter = cell->Begin(); iter != cell->End(); ++iter)
                    {
                        Object* obj = (*iter);
                        if (obj == nullptr)
                            continue;

                        if (!plr->IsVisible(obj->getGuid()) && plr->canSee(obj) && farsight->GetDistance2dSq(obj) <= m_UpdateDistance)
                        {
                            ByteBuffer buf;
                            uint32 count = obj->buildCreateUpdateBlockForPlayer(&buf, plr);
                            plr->getUpdateMgr().pushCreationData(&buf, count);
                            plr->m_visibleFarsightObjects.insert(obj);
                        }
                    }
                }
            }
        }
    }
}

bool MapMgr::runThread()
{
    bool rv = true;

    THREAD_TRY_EXECUTION
    rv = Do();
    THREAD_HANDLE_CRASH

    return rv;
}

bool MapMgr::Do()
{
#ifdef WIN32
    threadid = GetCurrentThreadId();
#endif

    t_currentMapContext.set(this);

    thread_running = true;
    ThreadState = THREADSTATE_BUSY;
    SetThreadName("Map mgr - M%u|I%u", this->_mapId, this->m_instanceID);

    uint32 last_exec = Util::getMSTime();

    // Create Instance script
    LoadInstanceScript();

    // create static objects
    for (auto& GameobjectSpawn : _map->staticSpawns.GameobjectSpawns)
    {
        GameObject* obj = CreateGameObject(GameobjectSpawn->entry);
        obj->Load(GameobjectSpawn);
        PushStaticObject(obj);
    }

    // Call script OnLoad virtual procedure
    CALL_INSTANCE_SCRIPT_EVENT(this, OnLoad)();

    for (auto& CreatureSpawn : _map->staticSpawns.CreatureSpawns)
    {
        Creature* obj = CreateCreature(CreatureSpawn->entry);
        obj->Load(CreatureSpawn, 0, pMapInfo);
        PushStaticObject(obj);
    }

    // load corpses
    sObjectMgr.LoadCorpses(this);
    worldstateshandler.InitWorldStates(sObjectMgr.GetWorldStatesForMap(_mapId));
    worldstateshandler.setObserver(this);

    while (GetThreadState() != THREADSTATE_TERMINATE && !_shutdown)
    {
        uint32 exec_start = Util::getMSTime();

        //////////////////////////////////////////////////////////////////////////////////////////
        //first push to world new objects
        m_objectinsertlock.Acquire();

        if (m_objectinsertpool.size())
        {
            for (auto o : m_objectinsertpool)
                o->PushToWorld(this);

            m_objectinsertpool.clear();
        }

        m_objectinsertlock.Release();
        //////////////////////////////////////////////////////////////////////////////////////////

        //Now update sessions of this map + objects
        _PerformObjectDuties();

        last_exec = Util::getMSTime();
        uint32 exec_time = last_exec - exec_start;
        if (exec_time < 20)  //mapmgr update period 20
            Arcemu::Sleep(20 - exec_time);

        // Check if we have to die :P
        if (InactiveMoveTime && UNIXTIME >= InactiveMoveTime)
            break;
    }

    // Teleport any left-over players out.
    TeleportPlayers();

    // Clear the instance's reference to us.
    if (m_battleground)
    {
        sBattlegroundManager.DeleteBattleground(m_battleground);
        sInstanceMgr.DeleteBattlegroundInstance(GetMapId(), GetInstanceID());
    }

    if (pInstance != nullptr)
    {
        // check for a non-raid instance, these expire after 10 minutes.
        if (GetMapInfo()->isDungeon() || pInstance->m_isBattleground)
        {
            pInstance->m_mapMgr = nullptr;
            sInstanceMgr._DeleteInstance(pInstance, true);
            pInstance = nullptr;
        }
        else
        {
            // just null out the pointer
            pInstance->m_mapMgr = nullptr;
        }
    }
    else if (GetMapInfo()->isNonInstanceMap())
    {
        sInstanceMgr.m_singleMaps[GetMapId()] = nullptr;
    }

    thread_running = false;
    if (thread_kill_only)
        return false;

    // delete ourselves
    delete this;

    // already deleted, so the threadpool doesn't have to.
    return false;
}

void MapMgr::BeginInstanceExpireCountdown()
{
    // so players getting removed don't overwrite us
    forced_expire = true;

    for (auto& itr : m_PlayerStorage)
    {
        if (!itr.second->raidgrouponlysent)
            itr.second->sendRaidGroupOnly(60000, 1);
    }

    // set our expire time to 60 seconds.
    InactiveMoveTime = UNIXTIME + 60;
}

void MapMgr::InstanceShutdown()
{
    pInstance = nullptr;
    SetThreadState(THREADSTATE_TERMINATE);
}

void MapMgr::KillThread()
{
    pInstance = nullptr;
    thread_kill_only = true;
    SetThreadState(THREADSTATE_TERMINATE);
    while(thread_running)
    {
        Arcemu::Sleep(100);
    }
}

void MapMgr::AddObject(Object* obj)
{
    m_objectinsertlock.Acquire();
    m_objectinsertpool.insert(obj);
    m_objectinsertlock.Release();
}

Unit* MapMgr::GetUnit(const uint64 & guid)
{
    if (guid == 0)
        return nullptr;

    WoWGuid wowGuid;
    wowGuid.Init(guid);

    switch (wowGuid.getHigh())
    {
        case HighGuid::Unit:
        case HighGuid::Vehicle:
            return GetCreature(wowGuid.getGuidLowPart());
        case HighGuid::Player:
            return GetPlayer(wowGuid.getGuidLowPart());
        case HighGuid::Pet:
            return GetPet(wowGuid.getGuidLowPart());
    }

    return nullptr;
}

Object* MapMgr::_GetObject(const uint64 & guid)
{
    if (!guid)
        return nullptr;

    WoWGuid wowGuid;
    wowGuid.Init(guid);

    switch (wowGuid.getHigh())
    {
        case HighGuid::GameObject:
            return GetGameObject(wowGuid.getGuidLowPart());
        case HighGuid::Unit:
        case HighGuid::Vehicle:
            return GetCreature(wowGuid.getGuidLowPart());
        case HighGuid::DynamicObject:
            return GetDynamicObject(wowGuid.getGuidLowPart());
        case HighGuid::Transporter:
            return sTransportHandler.getTransporter(wowGuid.getGuidLowPart());
        default:
            return GetUnit(guid);
    }
}

void MapMgr::_PerformObjectDuties()
{
    ++mLoopCounter;

    uint32 mstime = Util::getMSTime();
    uint32 difftime = mstime - lastUnitUpdate;

    if (difftime > 500)
        difftime = 500;

    // Update any events.
    // we make update of events before objects so in case there are 0 timediff events they do not get deleted after update but on next server update loop
    eventHolder.Update(difftime);

    // Update Transporters
    {
        difftime = mstime - lastTransportUpdate;
        for (auto itr = m_TransportStorage.begin(); itr != m_TransportStorage.end();)
        {
            Transporter* trans = *itr;
            ++itr;

            if (!trans || !trans->IsInWorld())
                continue;

            trans->Update(difftime);
        }
        lastTransportUpdate = mstime;
    }

    // Update creatures.
    {
        for (creature_iterator = activeCreatures.begin(); creature_iterator != activeCreatures.end();)
        {
            Creature* ptr = *creature_iterator;
            ++creature_iterator;
            ptr->Update(difftime);
        }

        for (pet_iterator = m_PetStorage.begin(); pet_iterator != m_PetStorage.end();)
        {
            Pet* ptr2 = pet_iterator->second;
            ++pet_iterator;
            ptr2->Update(difftime);
        }
    }

    // Update players.
    {
        for (auto itr = m_PlayerStorage.begin(); itr != m_PlayerStorage.end();)
        {
            Player* ptr = itr->second;
            ++itr;
            ptr->Update(difftime);
        }

        lastUnitUpdate = mstime;
    }

    // Dynamic objects are updated every 100ms
    // We take the pointer, increment, and update in this order because during the update the DynamicObject might get deleted,
    // rendering the iterator unincrementable. Which causes a crash!
    difftime = mstime - lastDynamicObjectUpdate;
    if (difftime >= 100)
    {
        for (auto itr = m_DynamicObjectStorage.begin(); itr != m_DynamicObjectStorage.end();)
        {
            DynamicObject* o = itr->second;
            ++itr;

            o->UpdateTargets();
        }

        lastDynamicObjectUpdate = mstime;
    }

    // Update gameobjects only every 200ms
    difftime = mstime - lastGameobjectUpdate;
    if (difftime >= 200)
    {
        for (auto itr = GOStorage.begin(); itr != GOStorage.end(); )
        {
            GameObject* gameobject = *itr;
            ++itr;
            if (gameobject != nullptr)
                gameobject->Update(difftime);
        }

        lastGameobjectUpdate = mstime;
    }

    // Sessions are updated on every second loop
    if (mLoopCounter % 2)
    {
        for (auto itr = Sessions.begin(); itr != Sessions.end();)
        {
            WorldSession* session = (*itr);
            auto it2 = itr;

            ++itr;

            if (session->GetInstance() != m_instanceID)
            {
                Sessions.erase(it2);
                continue;
            }

            // Don't update players not on our map.
            // If we abort in the handler, it means we will "lose" packets, or not process this.
            // .. and that could be disastrous to our client :P
            if (session->GetPlayer() && (session->GetPlayer()->GetMapMgr() != this && session->GetPlayer()->GetMapMgr() != nullptr))
            {
                continue;
            }

            uint8 result;

            if ((result = session->Update(m_instanceID)) != 0)
            {
                if (result == 1)
                {
                    // complete deletion
                    sWorld.deleteSession(session);
                }
                Sessions.erase(it2);
            }
        }
    }

    // Finally, A9 Building/Distribution
    _UpdateObjects();
}

void MapMgr::EventCorpseDespawn(uint64 guid)
{
    Corpse* pCorpse = sObjectMgr.GetCorpse((uint32)guid);
    if (pCorpse == nullptr)     // Already Deleted
        return;

    if (pCorpse->GetMapMgr() != this)
        return;

    pCorpse->Despawn();
    delete pCorpse;
}

void MapMgr::TeleportPlayers()
{
    for (auto itr = m_PlayerStorage.begin(); itr != m_PlayerStorage.end();)
    {
        Player* p = itr->second;
        ++itr;

        if (!bServerShutdown)
        {
            p->EjectFromInstance();
        }
        else
        {
            if (p->GetSession())
                p->GetSession()->LogoutPlayer(false);
            else
                delete p;
        }
    }
}

uint32 MapMgr::GetInstanceID()
{
    return m_instanceID;
}

MySQLStructure::MapInfo const* MapMgr::GetMapInfo()
{
    return pMapInfo;
}

MapScriptInterface* MapMgr::GetInterface()
{
    return ScriptInterface;
}

int32 MapMgr::event_GetInstanceID()
{
    return m_instanceID;
}

void MapMgr::UnloadCell(uint32 x, uint32 y)
{
    MapCell* c = GetCell(x, y);
    if (c == nullptr || c->HasPlayers() || _CellActive(x, y) || !c->IsUnloadPending())
        return;

    sLogger.debug("Unloading Cell [%u][%u] on map %u (instance %u)...", x, y, _mapId, m_instanceID);

    c->Unload();
}

void MapMgr::EventRespawnCreature(Creature* c, uint16 x, uint16 y)
{
    MapCell* cell = GetCell(x, y);
    if (cell == nullptr)    //cell got deleted while waiting for respawn.
        return;

    auto itr = cell->_respawnObjects.find(c);
    if (itr != cell->_respawnObjects.end())
    {
        c->m_respawnCell = nullptr;
        cell->_respawnObjects.erase(itr);
        c->OnRespawn(this);
    }
}

void MapMgr::EventRespawnGameObject(GameObject* o, uint16 x, uint16 y)
{
    MapCell* cell = GetCell(x, y);
    if (cell == nullptr)   //cell got deleted while waiting for respawn.
        return;

    auto itr = cell->_respawnObjects.find(o);
    if (itr != cell->_respawnObjects.end())
    {
        o->m_respawnCell = nullptr;
        cell->_respawnObjects.erase(itr);
        o->Spawn(this);
    }
}

void MapMgr::SendChatMessageToCellPlayers(Object* obj, WorldPacket* packet, uint32 cell_radius, uint32 langpos, int32 lang, WorldSession* originator)
{
    uint32 cellX = GetPosX(obj->GetPositionX());
    uint32 cellY = GetPosY(obj->GetPositionY());
    uint32 endX = ((cellX + cell_radius) <= _sizeX) ? cellX + cell_radius : (_sizeX - 1);
    uint32 endY = ((cellY + cell_radius) <= _sizeY) ? cellY + cell_radius : (_sizeY - 1);
    uint32 startX = cellX > cell_radius ? cellX - cell_radius : 0;
    uint32 startY = cellY > cell_radius ? cellY - cell_radius : 0;

    MapCell::ObjectSet::iterator iter, iend;
    for (uint32 posX = startX; posX <= endX; ++posX)
    {
        for (uint32 posY = startY; posY <= endY; ++posY)
        {
            MapCell* cell = GetCell(posX, posY);
            if (cell && cell->HasPlayers())
            {
                iter = cell->Begin();
                iend = cell->End();
                for (; iter != iend; ++iter)
                {
                    if ((*iter)->isPlayer())
                    {
                        //TO< Player* >(*iter)->GetSession()->SendPacket(packet);
                        if ((*iter)->GetPhase() & obj->GetPhase())
                            static_cast< Player* >(*iter)->GetSession()->SendChatPacket(packet, langpos, lang, originator);
                    }
                }
            }
        }
    }
}

Creature* MapMgr::GetSqlIdCreature(uint32 sqlid)
{
    auto itr = _sqlids_creatures.find(sqlid);
    return itr == _sqlids_creatures.end() ? nullptr : itr->second;
}

GameObject* MapMgr::GetSqlIdGameObject(uint32 sqlid)
{
    auto itr = _sqlids_gameobjects.find(sqlid);
    return itr == _sqlids_gameobjects.end() ? nullptr : itr->second;
}

uint64 MapMgr::GenerateCreatureGUID(uint32 entry, bool canUseOldGuid/* = true*/)
{
    uint64 newguid = 0;

    CreatureProperties const* creature_properties = sMySQLStore.getCreatureProperties(entry);
    if (creature_properties == nullptr || creature_properties->vehicleid == 0)
        newguid = static_cast<uint64>(HIGHGUID_TYPE_UNIT) << 32;
    else
        newguid = static_cast<uint64>(HIGHGUID_TYPE_VEHICLE) << 32;

    char* pHighGuid = reinterpret_cast<char*>(&newguid);
    char* pEntry = reinterpret_cast<char*>(&entry);

    pHighGuid[3] |= pEntry[0];
    pHighGuid[4] |= pEntry[1];
    pHighGuid[5] |= pEntry[2];
    pHighGuid[6] |= pEntry[3];

    uint32 guid = 0;

    if (!_reusable_guids_creature.empty() && canUseOldGuid)
    {
        guid = _reusable_guids_creature.front();
        _reusable_guids_creature.pop_front();

    }
    else
    {
        m_CreatureHighGuid++;

        if (m_CreatureHighGuid >= CreatureStorage.size())
        {
            // Reallocate array with larger size.
            size_t newsize = CreatureStorage.size() + RESERVE_EXPAND_SIZE;
            CreatureStorage.resize(newsize, NULL);
        }
        guid = m_CreatureHighGuid;
    }

    newguid |= guid;

    return newguid;
}

Creature* MapMgr::CreateCreature(uint32 entry)
{
    uint64 guid = GenerateCreatureGUID(entry);
    return new Creature(guid);
}

Creature* MapMgr::CreateAndSpawnCreature(uint32 pEntry, float pX, float pY, float pZ, float pO)
{
    auto* creature = CreateCreature(pEntry);
    const auto* cp = sMySQLStore.getCreatureProperties(pEntry);
    if (cp == nullptr)
    {
        delete creature;
        return nullptr;
    }

    creature->Load(cp, pX, pY, pZ, pO);
    creature->AddToWorld(this);
    return creature;
}

Creature* MapMgr::GetCreature(uint32 guid)
{
    if (guid > m_CreatureHighGuid)
        return nullptr;

    return CreatureStorage[guid];
}

Summon* MapMgr::CreateSummon(uint32 entry, SummonType type, uint32_t duration)
{
    // Generate always a new guid for totems, otherwise the totem timer bar will get messed up
    uint64 guid = GenerateCreatureGUID(entry, type != SUMMONTYPE_TOTEM);

    return sObjectMgr.createSummonByGuid(guid, type, duration);
}

// Spawns the object too, without which you can not interact with the object
GameObject* MapMgr::CreateAndSpawnGameObject(uint32 entryID, float x, float y, float z, float o, float scale)
{
    auto gameobject_info = sMySQLStore.getGameObjectProperties(entryID);
    if (gameobject_info == nullptr)
    {
        sLogger.debug("Error looking up entry in CreateAndSpawnGameObject");
        return nullptr;
    }

    sLogger.debug("CreateAndSpawnGameObject: By Entry '%u'", entryID);

    GameObject* go = CreateGameObject(entryID);

    //Player* chr = m_session->GetPlayer();
    uint32 mapid = GetMapId();
    // Setup game object
    go->CreateFromProto(entryID, mapid, x, y, z, o);
    go->setScale(scale);
    go->InitAI();
    go->PushToWorld(this);

    // Create spawn instance
    auto go_spawn = new MySQLStructure::GameobjectSpawn;
    go_spawn->entry = go->getEntry();
    go_spawn->id = sObjectMgr.GenerateGameObjectSpawnID();
    go_spawn->map = go->GetMapId();
    go_spawn->position_x = go->GetPositionX();
    go_spawn->position_y = go->GetPositionY();
    go_spawn->position_z = go->GetPositionZ();
    go_spawn->orientation = go->GetOrientation();
    go_spawn->rotation_0 = go->getParentRotation(0);
    go_spawn->rotation_1 = go->getParentRotation(1);
    go_spawn->rotation_2 = go->getParentRotation(2);
    go_spawn->rotation_3 = go->getParentRotation(3);
    go_spawn->state = go->getState();
    go_spawn->flags = go->getFlags();
    go_spawn->faction = go->getFactionTemplate();
    go_spawn->scale = go->getScale();
    //go_spawn->stateNpcLink = 0;
    go_spawn->phase = go->GetPhase();
    go_spawn->overrides = go->GetOverrides();

    uint32 cx = GetPosX(x);
    uint32 cy = GetPosY(y);

    GetBaseMap()->GetSpawnsListAndCreate(cx, cy)->GameobjectSpawns.push_back(go_spawn);
    go->m_spawn = go_spawn;

    MapCell* mCell = GetCell(cx, cy);

    if (mCell != nullptr)
        mCell->SetLoaded();

    return go;
}

GameObject* MapMgr::GetGameObject(uint32 guid)
{
    if (guid > m_GOHighGuid)
        return nullptr;

    return GOStorage[guid];
}

GameObject* MapMgr::CreateGameObject(uint32 entry)
{
    uint32 GUID = 0;

    if (_reusable_guids_gameobject.size() > GO_GUID_RECYCLE_INTERVAL)
    {
        uint32 guid = _reusable_guids_gameobject.front();
        _reusable_guids_gameobject.pop_front();

        GUID = guid;
    }
    else
    {
        if (++m_GOHighGuid >= GOStorage.size())
        {
            // Reallocate array with larger size.
            size_t newsize = GOStorage.size() + RESERVE_EXPAND_SIZE;
            GOStorage.resize(newsize, NULL);
        }

        GUID = m_GOHighGuid;
    }

    GameObject* gameobject = sObjectMgr.createGameObjectByGuid(entry, GUID);
    if (gameobject == nullptr)
        return nullptr;

    return gameobject;
}

DynamicObject* MapMgr::CreateDynamicObject()
{
    return new DynamicObject(HIGHGUID_TYPE_DYNAMICOBJECT, (++m_DynamicObjectHighGuid));
}

DynamicObject* MapMgr::GetDynamicObject(uint32 guid)
{
    auto itr = m_DynamicObjectStorage.find(guid);
    return itr != m_DynamicObjectStorage.end() ? itr->second : nullptr;
}

Pet* MapMgr::GetPet(uint32 guid)
{
    auto itr = m_PetStorage.find(guid);
    return itr != m_PetStorage.end() ? itr->second : nullptr;
}

Player* MapMgr::GetPlayer(uint32 guid)
{
    auto itr = m_PlayerStorage.find(guid);
    return itr != m_PlayerStorage.end() ? itr->second : nullptr;
}

void MapMgr::AddCombatInProgress(uint64 guid)
{
    _combatProgress.insert(guid);
}

void MapMgr::RemoveCombatInProgress(uint64 guid)
{
    _combatProgress.erase(guid);
}

void MapMgr::addForcedCell(MapCell* c)
{
    uint8_t cellNumber = worldConfig.server.mapCellNumber;

    m_forcedcells.insert(c);
    UpdateCellActivity(c->GetPositionX(), c->GetPositionY(), cellNumber);
}

void MapMgr::removeForcedCell(MapCell* c)
{
    uint8_t cellNumber = worldConfig.server.mapCellNumber;

    m_forcedcells.erase(c);
    UpdateCellActivity(c->GetPositionX(), c->GetPositionY(), cellNumber);
}

void MapMgr::addForcedCell(MapCell* c, uint32_t range)
{
    m_forcedcells.insert(c);
    UpdateCellActivity(c->GetPositionX(), c->GetPositionY(), range);
}

void MapMgr::removeForcedCell(MapCell* c, uint32_t range)
{
    m_forcedcells.erase(c);
    UpdateCellActivity(c->GetPositionX(), c->GetPositionY(), range);
}

bool MapMgr::cellHasAreaID(uint32_t CellX, uint32_t CellY, uint16_t &AreaID)
{
    int32_t TileX = CellX / 8;
    int32_t TileY = CellY / 8;

    if (!_terrain->areTilesValid(TileX, TileY))
        return false;

    int32_t OffsetTileX = TileX - _terrain->TileStartX;
    int32_t OffsetTileY = TileY - _terrain->TileStartY;

    bool Required = false;
    bool Result = false;

    if (!_terrain->tileLoaded(OffsetTileX, OffsetTileY))
        Required = true;

    if (Required)
    {
        _terrain->LoadTile(TileX, TileY);
        _terrain->LoadTile(TileX, TileY);
        return Result;
    }

    for (uint32_t xc = (CellX%CellsPerTile) * 16 / CellsPerTile; xc < (CellX%CellsPerTile) * 16 / CellsPerTile + 16 / CellsPerTile; xc++)
    {
        for (uint32_t yc = (CellY%CellsPerTile) * 16 / CellsPerTile; yc < (CellY%CellsPerTile) * 16 / CellsPerTile + 16 / CellsPerTile; yc++)
        {
            const auto areaid = _terrain->GetTile(OffsetTileX, OffsetTileY)->m_map.m_areaMap[yc * 16 + xc];
            if (areaid)
            {
                AreaID = areaid;
                Result = true;
                break;
            }
        }
    }

    if (Required)
        _terrain->UnloadTile(TileX, TileY);

    return Result;
}

void MapMgr::updateAllCells(bool apply, uint32_t areamask)
{
    uint16_t AreaID = 0;
    MapCell* cellInfo;
    CellSpawns* spawns;
    uint32_t StartX = 0, EndX = 0, StartY = 0, EndY = 0;
    _terrain->getCellLimits(StartX, EndX, StartY, EndY);

    if (!areamask)
        sLogger.debugFlag(AscEmu::Logging::LF_MAP_CELL, "Updating all cells for map %03u, server might lag.", GetMapId());

    for (uint32_t x = StartX; x < EndX; x++)
    {
        for (uint32_t y = StartY; y < EndY; y++)
        {
            if (areamask)
            {
                if (!cellHasAreaID(x, y, AreaID))
                    continue;

                auto at = sAreaStore.LookupEntry(AreaID);
                if (at == nullptr)
                    continue;
                if (at->zone != areamask)
                    if (at->id != areamask)
                        continue;
                AreaID = 0;
            }

            cellInfo = GetCell(x, y);
            if (apply)
            {
                if (!cellInfo)
                {   // Cell doesn't exist, create it.
                    cellInfo = Create(x, y);
                    cellInfo->Init(x, y, this);
                    sLogger.debugFlag(AscEmu::Logging::LF_MAP_CELL, "Created cell [%u,%u] on map %u (instance %u).", x, y, GetMapId(), m_instanceID);
                }
                
                spawns = _map->GetSpawnsList(x, y);
                if (spawns)
                {
                    addForcedCell(cellInfo, 1);

                    if (!cellInfo->IsLoaded())
                        cellInfo->LoadObjects(spawns);
                }
            }
            else
            {
                if (!cellInfo)
                    continue;

                removeForcedCell(cellInfo, 1);
            }
        }
    }

    if (!areamask)
        sLogger.debugFlag(AscEmu::Logging::LF_MAP_CELL, "Cell updating success for map %03u", _mapId);
}

void MapMgr::updateAllCells(bool apply)
{
    MapCell* cellInfo;
    CellSpawns* spawns;
    uint32_t StartX = 0, EndX = 0, StartY = 0, EndY = 0;
    _terrain->getCellLimits(StartX, EndX, StartY, EndY);

    for (uint32_t x = StartX; x < EndX; x++)
    {
        for (uint32_t y = StartY; y < EndY; y++)
        {
            cellInfo = GetCell(x, y);
            if (apply)
            {
                if (!cellInfo)
                {   // Cell doesn't exist, create it.
                    cellInfo = Create(x, y);
                    cellInfo->Init(x, y, this);
                    sLogger.debugFlag(AscEmu::Logging::LF_MAP_CELL, "Created cell [%u,%u] on map %u (instance %u).", x, y, GetMapId(), m_instanceID);
                }

                spawns = _map->GetSpawnsList(cellInfo->GetPositionX(), cellInfo->GetPositionY());
                if (spawns)
                {
                    addForcedCell(cellInfo, 1);

                    if (!cellInfo->IsLoaded())
                        cellInfo->LoadObjects(spawns);
                }
            }
            else
            {
                if (!cellInfo)
                    continue;

                removeForcedCell(cellInfo, 1);
            }
        }
    }
    sLogger.debugFlag(AscEmu::Logging::LF_MAP_CELL, "Cell updating success for map %03u", _mapId);
}

float MapMgr::GetFirstZWithCPZ(float x, float y, float z)
{
    if (!worldConfig.terrainCollision.isCollisionEnabled)
        return NO_WMO_HEIGHT;

    float posZ = NO_WMO_HEIGHT;
    for (int i = 2; i >= -2; i--)   //z range = 2
    {
        VMAP::IVMapManager* mgr = VMAP::VMapFactory::createOrGetVMapManager();
        //if (i== 0 && !IsUnderground(x,y,z)) return GetBaseMap()->GetLandHeight(x, y);
        posZ = mgr->getHeight(GetMapId(), x, y, z + (float)i, 10000.0f);
        if (posZ != NO_WMO_HEIGHT)
            break;
    }
    return posZ;
}

GameObject* MapMgr::FindNearestGoWithType(Object* o, uint32 type)
{
    GameObject* go = nullptr;
    float r = FLT_MAX;

    for (const auto& itr : o->getInRangeObjectsSet())
    {
        Object* iro = itr;
        if (!iro || !iro->isGameObject())
            continue;

        GameObject* irgo = static_cast<GameObject*>(iro);

        if (irgo->getGoType() != type)
            continue;

        if ((irgo->GetPhase() & o->GetPhase()) == 0)
            continue;

        float range = o->getDistanceSq(iro);

        if (range < r)
        {
            r = range;
            go = irgo;
        }
    }

    return go;
}

void MapMgr::SendPvPCaptureMessage(int32 ZoneMask, uint32 ZoneId, const char* Message, ...)
{
    va_list ap;
    va_start(ap, Message);

    char msgbuf[200];
    vsnprintf(msgbuf, 200, Message, ap);
    va_end(ap);

    for (auto itr = m_PlayerStorage.begin(); itr != m_PlayerStorage.end();)
    {
        Player* plr = itr->second;
        ++itr;

        if ((ZoneMask != ZONE_MASK_ALL && plr->GetZoneId() != (uint32)ZoneMask))
            continue;

        plr->GetSession()->SendPacket(SmsgDefenseMessage(ZoneId, msgbuf).serialise().get());
    }
}

void MapMgr::SendPacketToAllPlayers(WorldPacket* packet) const
{
    for (const auto& itr : m_PlayerStorage)
    {
        Player* p = itr.second;

        if (p->GetSession() != nullptr)
            p->GetSession()->SendPacket(packet);
    }
}

void MapMgr::SendPacketToPlayersInZone(uint32 zone, WorldPacket* packet) const
{
    for (const auto& itr : m_PlayerStorage)
    {
        Player* p = itr.second;

        if ((p->GetSession() != nullptr) && (p->GetZoneId() == zone))
            p->GetSession()->SendPacket(packet);
    }
}

InstanceScript* MapMgr::GetScript()
{
    return mInstanceScript;
}

void MapMgr::LoadInstanceScript()
{
    mInstanceScript = sScriptMgr.CreateScriptClassForInstance(_mapId, this);
};

void MapMgr::CallScriptUpdate()
{
    if (mInstanceScript != nullptr)
    {
        mInstanceScript->UpdateEvent();
        mInstanceScript->updateTimers();
    }
    else
    {
        sLogger.failure("MapMgr::CallScriptUpdate tries to call without valid instance script (nullptr)");
    }
};

uint32 MapMgr::GetAreaFlag(float x, float y, float z, bool * /*isOutdoors*/) const
{
    uint32 mogp_flags;
    int32 adt_id;
    int32 root_id;
    int32 group_id;

    bool have_area_info = _terrain->GetAreaInfo(x, y, z, mogp_flags, adt_id, root_id, group_id);
    auto area_flag_without_adt_id = _terrain->GetAreaFlagWithoutAdtId(x, y);
    return MapManagement::AreaManagement::AreaStorage::GetFlagByPosition(area_flag_without_adt_id, have_area_info, mogp_flags, adt_id, root_id, group_id, _mapId, x, y, z, nullptr);
}


WorldStatesHandler& MapMgr::GetWorldStatesHandler()
{
    return worldstateshandler;
}

void MapMgr::onWorldStateUpdate(uint32 zone, uint32 field, uint32 value)
{
    SendPacketToPlayersInZone(zone, SmsgUpdateWorldState(field, value).serialise().get());
}

bool MapMgr::AddToMapMgr(Transporter* obj)
{
    m_TransportStorage.insert(obj);

    return true;
}

void MapMgr::RemoveFromMapMgr(Transporter* obj, bool remove)
{
    m_TransportStorage.erase(obj);
    sTransportHandler.removeInstancedTransport(obj, this->GetInstanceID());

    RemoveObject(obj, false);

    if (remove)
        obj->RemoveFromWorld(true);
}
