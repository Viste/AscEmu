/*
Copyright (c) 2014-2021 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include "MapCell.h"
#include "CellHandler.h"
#include "Management/WorldStatesHandler.h"
#include "MapDefines.h"
#include "CThreads.h"
#include "Objects/Units/Creatures/Summons/SummonDefines.hpp"
#include "Server/EventableObject.h"
#include "Storage/DBC/DBCStructures.hpp"

namespace Arcemu
{
    namespace Utility
    {
        template<typename T>
        class TLSObject;
    }
}

template <typename T>
class CellHandler;
class Map;
class Object;
class MapScriptInterface;
class WorldSession;
class GameObject;
class Creature;
class Player;
class Pet;
class Transporter;
class Corpse;
class CBattleground;
class Instance;
class InstanceScript;
class Summon;
class DynamicObject;
class Unit;
class CreatureGroup;

extern Arcemu::Utility::TLSObject<MapMgr*> t_currentMapContext;

typedef std::set<Object*> ObjectSet;
typedef std::set<Object*> UpdateQueue;
typedef std::set<Player*> PUpdateQueue;
typedef std::set<Player*> PlayerSet;
typedef std::set<uint64> CombatProgressMap;
typedef std::set<Creature*> CreatureSet;
typedef std::set<GameObject*> GameObjectSet;

typedef std::unordered_map<uint32, Object*> StorageMap;
typedef std::unordered_map<uint32, Creature*> CreatureSqlIdMap;
typedef std::unordered_map<uint32, GameObject*> GameObjectSqlIdMap;

class SERVER_DECL MapMgr : public CellHandler <MapCell>, public EventableObject, public CThread, public WorldStatesHandler::WorldStatesObserver
{
    friend class MapCell;
    friend class MapScriptInterface;

public:

    uint32 GetAreaFlag(float x, float y, float z, bool *is_outdoors = nullptr) const;

    // This will be done in regular way soon
    std::set<MapCell*> m_forcedcells;

    void addForcedCell(MapCell* c);
    void removeForcedCell(MapCell* c);
    void addForcedCell(MapCell* c, uint32_t range);
    void removeForcedCell(MapCell* c, uint32_t range);

    bool cellHasAreaID(uint32_t x, uint32_t y, uint16_t &AreaID);
    void updateAllCells(bool apply, uint32_t areamask);
    void updateAllCells(bool apply);

    Mutex m_objectinsertlock;
    ObjectSet m_objectinsertpool;
    void AddObject(Object*);

    // Local (mapmgr) storage/generation of GameObjects
    uint32 m_GOHighGuid;
    std::vector<GameObject*> GOStorage;
    GameObject* CreateGameObject(uint32 entry);
    GameObject* CreateAndSpawnGameObject(uint32 entryID, float x, float y, float z, float o, float scale);

    uint32 GenerateGameobjectGuid() { return ++m_GOHighGuid; }

    GameObject* GetGameObject(uint32 guid);

    // Local (mapmgr) storage/generation of Transporters
    bool AddToMapMgr(Transporter* obj);
    void RemoveFromMapMgr(Transporter* obj, bool remove);

    // Local (mapmgr) storage/generation of Creatures
    uint32 m_CreatureHighGuid;
    std::vector<Creature*> CreatureStorage;
    CreatureSet::iterator creature_iterator;        /// required by owners despawning creatures and deleting *(++itr)
    uint64 GenerateCreatureGUID(uint32 entry, bool canUseOldGuid = true);
    Creature* CreateCreature(uint32 entry);
    Creature* CreateAndSpawnCreature(uint32 pEntry, float pX, float pY, float pZ, float pO);

    Creature* GetCreature(uint32 guid);

    //////////////////////////////////////////////////////////////////////////////////////////
    // Summon* CreateSummon(uint32 entry, SummonType type)
    // Summon factory function, creates and returns the appropriate summon subclass.
    //
    // @param uint32 entry      -  entry of the summon (NPC id)
    // @param SummonType type   -  Type of the summon
    // @param uint32_t duration -  Duration of the summon
    //
    // @return pointer to a summon
    Summon* CreateSummon(uint32 entry, SummonType type, uint32_t duration);

    // Local (mapmgr) storage/generation of DynamicObjects
    uint32 m_DynamicObjectHighGuid;
    typedef std::unordered_map<uint32, DynamicObject*> DynamicObjectStorageMap;
    DynamicObjectStorageMap m_DynamicObjectStorage;
    DynamicObject* CreateDynamicObject();

    DynamicObject* GetDynamicObject(uint32 guid);

    // Local (mapmgr) storage of pets
    typedef std::unordered_map<uint32, Pet*> PetStorageMap;
    PetStorageMap m_PetStorage;
    PetStorageMap::iterator pet_iterator;
    Pet* GetPet(uint32 guid);

    // Local (mapmgr) storage of players for faster lookup
    // double typedef lolz// a compile breaker..
    typedef std::unordered_map<uint32, Player*> PlayerStorageMap;
    PlayerStorageMap m_PlayerStorage;
    Player* GetPlayer(uint32 guid);

    // Local (mapmgr) storage of combats in progress
    CombatProgressMap _combatProgress;
    void AddCombatInProgress(uint64 guid);

    void RemoveCombatInProgress(uint64 guid);

    // Lookup Wrappers
    Unit* GetUnit(const uint64 & guid);
    Object* _GetObject(const uint64 & guid);

    bool runThread() override;
    bool Do();

    MapMgr(Map* map, uint32 mapid, uint32 instanceid);
    ~MapMgr();

    void PushObject(Object* obj);
    void PushStaticObject(Object* obj);
    void RemoveObject(Object* obj, bool free_guid);
    void ChangeObjectLocation(Object* obj); // update inrange lists
    void ChangeFarsightLocation(Player* plr, DynamicObject* farsight);

    // Mark object as updated
    void ObjectUpdated(Object* obj);
    void UpdateCellActivity(uint32 x, uint32 y, uint32 radius);
    void respawnBossLinkedGroups(uint32_t bossId);
    void spawnManualGroup(uint32_t groupId);

    // Terrain Functions
    float GetLandHeight(float x, float y, float z);

    float getWaterOrGroundLevel(uint32 phasemask, float x, float y, float z, float* ground = nullptr, bool swim = false, float collisionHeight = 2.03128f); // DEFAULT_COLLISION_HEIGHT in Object.h

    float GetADTLandHeight(float x, float y);

    bool IsUnderground(float x, float y, float z);

    bool GetLiquidInfo(float x, float y, float z, float& liquidlevel, uint32& liquidtype);

    float GetLiquidHeight(float x, float y);

    uint8 GetLiquidType(float x, float y);

    bool isUnderWater(float x, float y, float z);

    ZLiquidStatus getLiquidStatus(uint32 phaseMask, float x, float y, float z, uint8 ReqLiquidType, LiquidData* data = nullptr, float collisionHeight = 2.03128f); // DEFAULT_COLLISION_HEIGHT in Object.h

    const ::DBC::Structures::AreaTableEntry* GetArea(float x, float y, float z);

    bool isInLineOfSight(float x, float y, float z, float x2, float y2, float z2);

    uint32 GetMapId();

    void PushToProcessed(Player* plr);

    bool HasPlayers();

    bool IsCombatInProgress();
    void TeleportPlayers();

    uint32 GetInstanceID();

    MySQLStructure::MapInfo const* GetMapInfo();

    bool _shutdown;

    MapScriptInterface* GetInterface();

    int32_t event_GetInstanceID() override;

    uint32 GetPlayerCount();

    void _PerformObjectDuties();
    uint32 mLoopCounter;
    uint32 lastGameobjectUpdate;
    uint32 lastTransportUpdate;
    uint32_t lastDynamicObjectUpdate = 0;
    uint32 lastUnitUpdate;
    void EventCorpseDespawn(uint64 guid);

    time_t InactiveMoveTime;
    uint8 iInstanceMode;

    void UnloadCell(uint32 x, uint32 y);
    void EventRespawnCreature(Creature* c, uint16 x, uint16 y);
    void EventRespawnGameObject(GameObject* o, uint16 x, uint16 y);
    void SendChatMessageToCellPlayers(Object* obj, WorldPacket* packet, uint32 cell_radius, uint32 langpos, int32 lang, WorldSession* originator);
    void SendPvPCaptureMessage(int32 ZoneMask, uint32 ZoneId, const char* Message, ...);
    void SendPacketToAllPlayers(WorldPacket* packet) const;
    void SendPacketToPlayersInZone(uint32 zone, WorldPacket* packet) const;

    Instance* pInstance;
    void BeginInstanceExpireCountdown();

    // better hope to clear any references to us when calling this :P
    void InstanceShutdown();

    /// kill the worker thread only
    void KillThread();

    float GetFirstZWithCPZ(float x, float y, float z);

    // Finds and returns the nearest GameObject with this type from Object's inrange set.
    // @param    Object* o - Pointer to the Object that's inrange set we are searching
    // @param    uint32 type - Type of the GameObject we want to find
    // @return a pointer to the GameObject if found, NULL if there isn't such a GameObject.
    GameObject* FindNearestGoWithType(Object* o, uint32 type);

protected:
    /// Collect and send updates to clients
    void _UpdateObjects();

    typedef std::set<Transporter*> TransportsContainer;
    TransportsContainer m_TransportStorage;

private:
    // Objects that exist on map
    uint32 _mapId;
    std::set<Object*> _mapWideStaticObjects;

    bool _CellActive(uint32 x, uint32 y);
    void UpdateInRangeSet(Object* obj, Player* plObj, MapCell* cell, ByteBuffer** buf);

    //Zyres: Refactoring 05/04/2016
    float GetUpdateDistance(Object* curObj, Object* obj, Player* plObj);
    void OutOfMapBoundariesTeleport(Object* object);

public:
    /// Distance a Player can "see" other objects and receive updates from them (!! ALREADY dist*dist !!)
    float m_UpdateDistance;

private:
    // Update System
    Mutex m_updateMutex;
    UpdateQueue _updates;
    PUpdateQueue _processQueue;

    // Sessions
    std::set<WorldSession*> Sessions;

    // Map Information
    MySQLStructure::MapInfo const* pMapInfo;
    uint32 m_instanceID;

    MapScriptInterface* ScriptInterface;

    TerrainHolder* _terrain;

public:
#ifdef WIN32
    DWORD threadid;
#endif
    GameObjectSet activeGameObjects;
    CreatureSet activeCreatures;
    EventableObjectHolder eventHolder;
    CBattleground* m_battleground;
    std::set<Corpse*> m_corpses;
    CreatureSqlIdMap _sqlids_creatures;
    GameObjectSqlIdMap _sqlids_gameobjects;

    // Script related
    InstanceScript* GetScript();
    void LoadInstanceScript();
    void CallScriptUpdate();

    Creature* GetSqlIdCreature(uint32 sqlid);
    GameObject* GetSqlIdGameObject(uint32 sqlid);
    std::deque<uint32> _reusable_guids_gameobject;
    std::deque<uint32> _reusable_guids_creature;

    std::unordered_map<uint32_t /*leaderSpawnId*/, CreatureGroup*> CreatureGroupHolder;

    bool forced_expire;
    bool thread_kill_only;
    bool thread_running;

    WorldStatesHandler& GetWorldStatesHandler();

    void onWorldStateUpdate(uint32 zone, uint32 field, uint32 value) override;

protected:
    InstanceScript* mInstanceScript;

private:
    WorldStatesHandler worldstateshandler;
};
