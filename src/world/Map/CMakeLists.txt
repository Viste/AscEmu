# Copyright (c) 2014-2021 AscEmu Team <http://www.ascemu.org>

set(PATH_PREFIX Map)

set(SRC_MAP_FILES
    ${PATH_PREFIX}/CellHandler.h
    ${PATH_PREFIX}/CellHandlerDefines.hpp
    ${PATH_PREFIX}/Instance.cpp
    ${PATH_PREFIX}/Instance.h
    ${PATH_PREFIX}/InstanceDefines.hpp
    ${PATH_PREFIX}/InstanceMgr.cpp
    ${PATH_PREFIX}/InstanceMgr.h
    ${PATH_PREFIX}/Map.cpp
    ${PATH_PREFIX}/Map.h
    ${PATH_PREFIX}/MapCell.cpp
    ${PATH_PREFIX}/MapCell.h
    ${PATH_PREFIX}/MapManagementGlobals.hpp
    ${PATH_PREFIX}/MapMgr.cpp
    ${PATH_PREFIX}/MapMgr.h
    ${PATH_PREFIX}/MapMgrDefines.hpp
    ${PATH_PREFIX}/MapScriptInterface.cpp
    ${PATH_PREFIX}/MapScriptInterface.h
    ${PATH_PREFIX}/RecastIncludes.hpp
    ${PATH_PREFIX}/TerrainMgr.cpp
    ${PATH_PREFIX}/TerrainMgr.h
    ${PATH_PREFIX}/WorldCreator.cpp
    ${PATH_PREFIX}/WorldCreator.h
    ${PATH_PREFIX}/WorldCreatorDefines.hpp
    ${PATH_PREFIX}/AreaBoundary.cpp
    ${PATH_PREFIX}/AreaBoundary.h
    ${PATH_PREFIX}/SpawnGroups.hpp
)

source_group(Map FILES ${SRC_MAP_FILES})
unset(PATH_PREFIX)
