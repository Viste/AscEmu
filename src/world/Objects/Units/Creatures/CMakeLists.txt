# Copyright (c) 2014-2021 AscEmu Team <http://www.ascemu.org>

set(PATH_PREFIX Objects/Units/Creatures)

set(SRC_UNITS_CREATURES_FILES
    ${PATH_PREFIX}/AIEvents.cpp
    ${PATH_PREFIX}/AIEvents.h
    ${PATH_PREFIX}/AIInterface.cpp 
    ${PATH_PREFIX}/AIInterface.h
    ${PATH_PREFIX}/Corpse.cpp
    ${PATH_PREFIX}/Corpse.h
    ${PATH_PREFIX}/Creature.cpp
    ${PATH_PREFIX}/Creature.h
    ${PATH_PREFIX}/CreatureDefines.hpp
    ${PATH_PREFIX}/Pet.cpp
    ${PATH_PREFIX}/Pet.h
    ${PATH_PREFIX}/Vehicle.cpp
    ${PATH_PREFIX}/Vehicle.h
    ${PATH_PREFIX}/CreatureGroups.cpp
    ${PATH_PREFIX}/CreatureGroups.h
)

source_group(Objects\\Units\\Creatures FILES ${SRC_UNITS_CREATURES_FILES})
unset(PATH_PREFIX)
