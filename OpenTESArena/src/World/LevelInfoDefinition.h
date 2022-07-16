#ifndef LEVEL_INFO_DEFINITION_H
#define LEVEL_INFO_DEFINITION_H

#include <string>
#include <unordered_map>
#include <vector>

#include "DoorDefinition.h"
#include "LevelDefinition.h"
#include "LockDefinition.h"
#include "MapGeneration.h"
#include "TransitionDefinition.h"
#include "TriggerDefinition.h"
#include "VoxelDefinition.h"
#include "VoxelMeshDefinition.h"
#include "VoxelTextureDefinition.h"
#include "VoxelUtils.h"
#include "../Entities/EntityDefinition.h"

// Modern replacement for .INF files; defines the actual voxels, entities, etc. pointed to by a
// level definition. This is intended to separate the level's IDs from what they're pointing to
// so it's easier to change climates, etc..

class LevelInfoDefinition
{
private:
	// Definitions pointed to by a level definition. These should all be engine-independent now
	// (meaning that they could theoretically work with a standalone editor).
	std::vector<VoxelDefinition> voxelDefs;
	std::vector<VoxelMeshDefinition> voxelMeshDefs; // @todo: make sure a player-created chasm voxel mesh exists
	std::vector<VoxelTextureDefinition> voxelTextureDefs; // @todo: make sure a player-created chasm voxel texture exists
	std::vector<EntityDefinition> entityDefs;
	std::vector<LockDefinition> lockDefs;
	std::vector<TriggerDefinition> triggerDefs;
	std::vector<TransitionDefinition> transitionDefs;
	std::vector<std::string> buildingNames;
	std::unordered_map<LevelDefinition::BuildingNameID, std::string> buildingNameOverrides;
	std::vector<DoorDefinition> doorDefs;

	// @todo: interior gen info ID for when player creates a wall on water.

	double ceilingScale; // Vertical size of walls; 1.0 by default.
public:
	LevelInfoDefinition();

	void init(double ceilingScale);

	int getVoxelDefCount() const;
	int getVoxelMeshDefCount() const;
	int getVoxelTextureDefCount() const;
	int getEntityDefCount() const;
	int getLockDefCount() const;
	int getTriggerDefCount() const;
	int getTransitionDefCount() const;
	int getBuildingNameCount() const;
	int getDoorDefCount() const;

	const VoxelDefinition &getVoxelDef(LevelDefinition::VoxelDefID id) const;
	const VoxelMeshDefinition &getVoxelMeshDef(LevelDefinition::VoxelMeshDefID id) const;
	const VoxelTextureDefinition &getVoxelTextureDef(LevelDefinition::VoxelTextureDefID id) const;
	const EntityDefinition &getEntityDef(LevelDefinition::EntityDefID id) const;
	const LockDefinition &getLockDef(LevelDefinition::LockDefID id) const;
	const TriggerDefinition &getTriggerDef(LevelDefinition::TriggerDefID id) const;
	const TransitionDefinition &getTransitionDef(LevelDefinition::TransitionDefID id) const;
	const std::string &getBuildingName(LevelDefinition::BuildingNameID id) const;
	const DoorDefinition &getDoorDef(LevelDefinition::DoorDefID id) const;
	double getCeilingScale() const;

	LevelDefinition::VoxelDefID addVoxelDef(VoxelDefinition &&def);
	LevelDefinition::VoxelMeshDefID addVoxelMeshDef(VoxelMeshDefinition &&def);
	LevelDefinition::VoxelTextureDefID addVoxelTextureDef(VoxelTextureDefinition &&def);
	LevelDefinition::EntityDefID addEntityDef(EntityDefinition &&def);
	LevelDefinition::LockDefID addLockDef(LockDefinition &&def);
	LevelDefinition::TriggerDefID addTriggerDef(TriggerDefinition &&def);
	LevelDefinition::TransitionDefID addTransitionDef(TransitionDefinition &&def);
	LevelDefinition::BuildingNameID addBuildingName(std::string &&name);
	LevelDefinition::DoorDefID addDoorDef(DoorDefinition &&def);

	// Handles some special cases in main quest cities.
	void setBuildingNameOverride(LevelDefinition::BuildingNameID id, std::string &&name);
};

#endif
