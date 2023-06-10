#include "EntityChunkManager.h"
#include "EntityDefinitionLibrary.h"
#include "EntityVisibilityState.h"
#include "Player.h"
#include "../Assets/ArenaAnimUtils.h"
#include "../Assets/BinaryAssetLibrary.h"
#include "../Assets/MIFUtils.h"
#include "../Assets/TextureManager.h"
#include "../Audio/AudioManager.h"
#include "../Game/CardinalDirection.h"
#include "../Math/Constants.h"
#include "../Math/RandomUtils.h"
#include "../Math/Random.h"
#include "../Rendering/Renderer.h"
#include "../Voxels/VoxelChunk.h"
#include "../Voxels/VoxelChunkManager.h"
#include "../World/ChunkUtils.h"
#include "../World/LevelDefinition.h"
#include "../World/LevelInfoDefinition.h"
#include "../World/MapDefinition.h"
#include "../World/MapType.h"

#include "components/utilities/String.h"

namespace
{
	Buffer<ScopedObjectTextureRef> MakeAnimTextureRefs(const EntityAnimationDefinition &animDef, TextureManager &textureManager, Renderer &renderer)
	{
		const int keyframeCount = animDef.keyframeCount;
		Buffer<ScopedObjectTextureRef> animTextureRefs(keyframeCount);

		for (int i = 0; i < keyframeCount; i++)
		{
			const EntityAnimationDefinitionKeyframe &keyframe = animDef.keyframes[i];
			const TextureAsset &textureAsset = keyframe.textureAsset;
			const std::optional<TextureBuilderID> textureBuilderID = textureManager.tryGetTextureBuilderID(textureAsset);
			if (!textureBuilderID.has_value())
			{
				DebugLogWarning("Couldn't load entity anim texture \"" + textureAsset.filename + "\".");
				continue;
			}

			const TextureBuilder &textureBuilder = textureManager.getTextureBuilderHandle(*textureBuilderID);
			ObjectTextureID textureID;
			if (!renderer.tryCreateObjectTexture(textureBuilder, &textureID))
			{
				DebugLogWarning("Couldn't create entity anim texture \"" + textureAsset.filename + "\".");
				continue;
			}

			ScopedObjectTextureRef textureRef(textureID, renderer);
			animTextureRefs.set(i, std::move(textureRef));
		}

		return animTextureRefs;
	}
}

const EntityDefinition &EntityChunkManager::getEntityDef(EntityDefID defID, const EntityDefinitionLibrary &defLibrary) const
{
	const auto iter = this->entityDefs.find(defID);
	if (iter != this->entityDefs.end())
	{
		return iter->second;
	}
	else
	{
		return defLibrary.getDefinition(defID);
	}
}

EntityDefID EntityChunkManager::addEntityDef(EntityDefinition &&def, const EntityDefinitionLibrary &defLibrary)
{
	const int libraryDefCount = defLibrary.getDefinitionCount();
	const EntityDefID defID = static_cast<EntityDefID>(libraryDefCount + this->entityDefs.size());
	this->entityDefs.emplace(defID, std::move(def));
	return defID;
}

EntityDefID EntityChunkManager::getOrAddEntityDefID(const EntityDefinition &def, const EntityDefinitionLibrary &defLibrary)
{
	for (const auto &pair : this->entityDefs)
	{
		const EntityDefID currentDefID = pair.first;
		const EntityDefinition &currentDef = pair.second;
		if (currentDef == def) // There doesn't seem to be a better way than value comparisons.
		{
			return currentDefID;
		}
	}

	return this->addEntityDef(EntityDefinition(def), defLibrary);
}

EntityInstanceID EntityChunkManager::spawnEntity()
{
	EntityInstanceID instID;
	if (!this->entities.tryAlloc(&instID))
	{
		DebugCrash("Couldn't allocate EntityInstanceID.");
	}

	return instID;
}

void EntityChunkManager::populateChunkEntities(EntityChunk &entityChunk, const VoxelChunk &voxelChunk,
	const LevelDefinition &levelDefinition, const LevelInfoDefinition &levelInfoDefinition, const WorldInt2 &levelOffset,
	const EntityGeneration::EntityGenInfo &entityGenInfo, const std::optional<CitizenUtils::CitizenGenInfo> &citizenGenInfo,
	Random &random, const EntityDefinitionLibrary &entityDefLibrary, const BinaryAssetLibrary &binaryAssetLibrary,
	TextureManager &textureManager, Renderer &renderer)
{
	SNInt startX, endX;
	int startY, endY;
	WEInt startZ, endZ;
	ChunkUtils::GetWritingRanges(levelOffset, levelDefinition.getWidth(), levelDefinition.getHeight(),
		levelDefinition.getDepth(), &startX, &startY, &startZ, &endX, &endY, &endZ);

	const double ceilingScale = levelInfoDefinition.getCeilingScale();

	for (int i = 0; i < levelDefinition.getEntityPlacementDefCount(); i++)
	{
		const LevelDefinition::EntityPlacementDef &placementDef = levelDefinition.getEntityPlacementDef(i);
		const LevelDefinition::EntityDefID levelEntityDefID = placementDef.id;
		const EntityDefinition &entityDef = levelInfoDefinition.getEntityDef(levelEntityDefID);
		const EntityDefinition::Type entityDefType = entityDef.getType();
		const bool isDynamicEntity = EntityUtils::isDynamicEntity(entityDefType);
		const EntityAnimationDefinition &animDef = entityDef.getAnimDef();

		const std::string &defaultAnimStateName = EntityGeneration::getDefaultAnimationStateName(entityDef, entityGenInfo);
		const std::optional<int> defaultAnimStateIndex = animDef.tryGetStateIndex(defaultAnimStateName.c_str());
		if (!defaultAnimStateIndex.has_value())
		{
			DebugLogWarning("Couldn't get default anim state index for entity.");
			continue;
		}

		std::optional<EntityDefID> entityDefID; // Global entity def ID (shared across all active chunks).
		for (const WorldDouble3 &position : placementDef.positions)
		{
			const WorldInt3 voxelPosition = VoxelUtils::pointToVoxel(position, ceilingScale);
			if (ChunkUtils::IsInWritingRange(voxelPosition, startX, endX, startY, endY, startZ, endZ))
			{
				if (!entityDefID.has_value())
				{
					entityDefID = this->getOrAddEntityDefID(entityDef, entityDefLibrary);
				}

				const VoxelDouble3 point = ChunkUtils::MakeChunkPointFromLevel(position, startX, startY, startZ);
				EntityInstanceID entityInstID = this->spawnEntity();
				EntityInstance &entityInst = this->entities.get(entityInstID);

				EntityPositionID positionID;
				if (!this->positions.tryAlloc(&positionID))
				{
					DebugCrash("Couldn't allocate EntityPositionID.");
				}

				EntityBoundingBoxID bboxID;
				if (!this->boundingBoxes.tryAlloc(&bboxID))
				{
					DebugCrash("Couldn't allocate EntityBoundingBoxID.");
				}

				entityInst.init(entityInstID, *entityDefID, positionID, bboxID);

				CoordDouble2 &entityCoord = this->positions.get(positionID);
				entityCoord.chunk = voxelChunk.getPosition();
				entityCoord.point = VoxelDouble2(point.x, point.z);

				double animMaxWidth, dummyAnimMaxHeight;
				EntityUtils::getAnimationMaxDims(animDef, &animMaxWidth, &dummyAnimMaxHeight);
				double &entityBBoxExtent = this->boundingBoxes.get(bboxID);
				entityBBoxExtent = animMaxWidth;

				if (isDynamicEntity) // Dynamic entities have a direction.
				{
					if (!this->directions.tryAlloc(&entityInst.directionID))
					{
						DebugCrash("Couldn't allocate EntityDirectionID.");
					}

					VoxelDouble2 &entityDir = this->directions.get(entityInst.directionID);
					entityDir = CardinalDirection::North;

					if (entityDefType == EntityDefinition::Type::Enemy)
					{
						if (!this->creatureSoundInsts.tryAlloc(&entityInst.creatureSoundInstID))
						{
							DebugCrash("Couldn't allocate EntityCreatureSoundInstanceID.");
						}

						double &secondsTillCreatureSound = this->creatureSoundInsts.get(entityInst.creatureSoundInstID);
						secondsTillCreatureSound = EntityUtils::nextCreatureSoundWaitTime(random);
					}
				}

				if (!this->animInsts.tryAlloc(&entityInst.animInstID))
				{
					DebugCrash("Couldn't allocate EntityAnimationInstanceID.");
				}

				EntityAnimationInstance &animInst = this->animInsts.get(entityInst.animInstID);

				// Populate anim inst states now so the def doesn't need to be provided later.
				for (int animDefStateIndex = 0; animDefStateIndex < animDef.stateCount; animDefStateIndex++)
				{
					const EntityAnimationDefinitionState &animDefState = animDef.states[animDefStateIndex];
					animInst.addState(animDefState.seconds, animDefState.isLooping);
				}

				const EntityAnimationDefinitionState &defaultAnimDefState = animDef.states[*defaultAnimStateIndex];
				animInst.setStateIndex(*defaultAnimStateIndex);

				entityChunk.entityIDs.emplace_back(entityInstID);
			}
		}
	}

	if (citizenGenInfo.has_value())
	{
		// Spawn citizens if the total active limit has not been reached.
		const int currentCitizenCount = CitizenUtils::getCitizenCount(*this);
		const int remainingCitizensToSpawn = std::min(CitizenUtils::MAX_ACTIVE_CITIZENS - currentCitizenCount, CitizenUtils::CITIZENS_PER_CHUNK);

		// @todo: spawn X male citizens then Y female citizens instead of randomly switching between defs/anim defs/etc.

		const ChunkInt2 &chunkPos = voxelChunk.getPosition();
		auto trySpawnCitizenInChunk = [this, &entityChunk, &voxelChunk, &entityGenInfo, &citizenGenInfo, &random,
			&binaryAssetLibrary, &textureManager, &chunkPos]()
		{
			const std::optional<VoxelInt2> spawnVoxel = [&voxelChunk, &random]() -> std::optional<VoxelInt2>
			{
				DebugAssert(voxelChunk.getHeight() >= 2);

				constexpr int spawnTriesCount = 20;
				for (int spawnTry = 0; spawnTry < spawnTriesCount; spawnTry++)
				{
					const VoxelInt2 spawnVoxel(random.next(Chunk::WIDTH), random.next(Chunk::DEPTH));
					const VoxelChunk::VoxelTraitsDefID voxelTraitsDefID = voxelChunk.getTraitsDefID(spawnVoxel.x, 1, spawnVoxel.y);
					const VoxelChunk::VoxelTraitsDefID groundVoxelTraitsDefID = voxelChunk.getTraitsDefID(spawnVoxel.x, 0, spawnVoxel.y);
					const VoxelTraitsDefinition &voxelTraitsDef = voxelChunk.getTraitsDef(voxelTraitsDefID);
					const VoxelTraitsDefinition &groundVoxelTraitsDef = voxelChunk.getTraitsDef(groundVoxelTraitsDefID);

					// @todo: this type check could ostensibly be replaced with some "allowsCitizenSpawn".
					if ((voxelTraitsDef.type == ArenaTypes::VoxelType::None) &&
						(groundVoxelTraitsDef.type == ArenaTypes::VoxelType::Floor))
					{
						return spawnVoxel;
					}
				}

				// No spawn position found.
				return std::nullopt;
			}();

			if (!spawnVoxel.has_value())
			{
				DebugLogWarning("Couldn't find spawn voxel for citizen.");
				return false;
			}

			const bool isMale = random.next(2) == 0;
			const EntityDefID entityDefID = isMale ? citizenGenInfo->maleEntityDefID : citizenGenInfo->femaleEntityDefID;
			const EntityDefinition &entityDef = isMale ? *citizenGenInfo->maleEntityDef : *citizenGenInfo->femaleEntityDef;
			const EntityAnimationDefinition &entityAnimDef = entityDef.getAnimDef();

			EntityInstanceID entityInstID = this->spawnEntity();
			EntityInstance &entityInst = this->entities.get(entityInstID);

			EntityPositionID positionID;
			if (!this->positions.tryAlloc(&positionID))
			{
				DebugLogError("Couldn't allocate citizen EntityPositionID.");
				return false;
			}

			EntityBoundingBoxID bboxID;
			if (!this->boundingBoxes.tryAlloc(&bboxID))
			{
				DebugLogError("Couldn't allocate citizen EntityBoundingBoxID.");
				return false;
			}

			entityInst.init(entityInstID, entityDefID, positionID, bboxID);

			CoordDouble2 &entityCoord = this->positions.get(positionID);
			entityCoord = CoordDouble2(chunkPos, VoxelUtils::getVoxelCenter(*spawnVoxel));

			double animMaxWidth, dummyAnimMaxHeight;
			EntityUtils::getAnimationMaxDims(entityAnimDef, &animMaxWidth, &dummyAnimMaxHeight);
			double &entityBBoxExtent = this->boundingBoxes.get(bboxID);
			entityBBoxExtent = animMaxWidth;

			if (!this->citizenDirectionIndices.tryAlloc(&entityInst.citizenDirectionIndexID))
			{
				DebugLogError("Couldn't allocate citizen EntityCitizenDirectionIndexID.");
				return false;
			}

			int8_t &citizenDirIndex = this->citizenDirectionIndices.get(entityInst.citizenDirectionIndexID);
			citizenDirIndex = CitizenUtils::getRandomCitizenDirectionIndex(random);

			if (!this->directions.tryAlloc(&entityInst.directionID))
			{
				DebugLogError("Couldn't allocate citizen EntityDirectionID.");
				return false;
			}

			VoxelDouble2 &entityDir = this->directions.get(entityInst.directionID);
			entityDir = CitizenUtils::getCitizenDirectionByIndex(citizenDirIndex);

			if (!this->animInsts.tryAlloc(&entityInst.animInstID))
			{
				DebugLogError("Couldn't allocate citizen EntityAnimationInstanceID.");
				return false;
			}

			EntityAnimationInstance &animInst = this->animInsts.get(entityInst.animInstID);
			animInst = isMale ? citizenGenInfo->maleAnimInst : citizenGenInfo->femaleAnimInst;

			if (!this->palettes.tryAlloc(&entityInst.paletteInstID))
			{
				DebugLogError("Couldn't allocate citizen EntityPaletteInstanceID.");
				return false;
			}

			const Palette &srcPalette = textureManager.getPaletteHandle(citizenGenInfo->paletteID);
			const uint16_t colorSeed = static_cast<uint16_t>(random.next() % std::numeric_limits<uint16_t>::max());

			Palette &palette = this->palettes.get(entityInst.paletteInstID);
			palette = ArenaAnimUtils::transformCitizenColors(citizenGenInfo->raceID, colorSeed, srcPalette, binaryAssetLibrary.getExeData());

			entityChunk.entityIDs.emplace_back(entityInstID);

			return true;
		};

		for (int i = 0; i < remainingCitizensToSpawn; i++)
		{
			if (!trySpawnCitizenInChunk())
			{
				DebugLogWarning("Couldn't spawn citizen in chunk \"" + chunkPos.toString() + "\".");
			}
		}
	}
}

void EntityChunkManager::populateChunk(EntityChunk &entityChunk, const VoxelChunk &voxelChunk,
	const LevelDefinition &levelDef, const LevelInfoDefinition &levelInfoDef, const MapSubDefinition &mapSubDef, 
	const EntityGeneration::EntityGenInfo &entityGenInfo, const std::optional<CitizenUtils::CitizenGenInfo> &citizenGenInfo,
	double ceilingScale, Random &random, const EntityDefinitionLibrary &entityDefLibrary,
	const BinaryAssetLibrary &binaryAssetLibrary, TextureManager &textureManager, Renderer &renderer)
{
	const ChunkInt2 &chunkPos = entityChunk.getPosition();
	const SNInt levelWidth = levelDef.getWidth();
	const WEInt levelDepth = levelDef.getDepth();

	// Populate all or part of the chunk from a level definition depending on the map type.
	const MapType mapType = mapSubDef.type;
	if (mapType == MapType::Interior)
	{
		DebugAssert(!citizenGenInfo.has_value());

		if (ChunkUtils::touchesLevelDimensions(chunkPos, levelWidth, levelDepth))
		{
			// Populate chunk from the part of the level it overlaps.
			const WorldInt2 levelOffset = chunkPos * ChunkUtils::CHUNK_DIM;
			this->populateChunkEntities(entityChunk, voxelChunk, levelDef, levelInfoDef, levelOffset, entityGenInfo,
				citizenGenInfo, random, entityDefLibrary, binaryAssetLibrary, textureManager, renderer);
		}
	}
	else if (mapType == MapType::City)
	{
		DebugAssert(citizenGenInfo.has_value());

		if (ChunkUtils::touchesLevelDimensions(chunkPos, levelWidth, levelDepth))
		{
			// Populate chunk from the part of the level it overlaps.
			const WorldInt2 levelOffset = chunkPos * ChunkUtils::CHUNK_DIM;
			this->populateChunkEntities(entityChunk, voxelChunk, levelDef, levelInfoDef, levelOffset, entityGenInfo,
				citizenGenInfo, random, entityDefLibrary, binaryAssetLibrary, textureManager, renderer);
		}
	}
	else if (mapType == MapType::Wilderness)
	{
		DebugAssert(levelDef.getWidth() == Chunk::WIDTH);
		DebugAssert(levelDef.getDepth() == Chunk::DEPTH);
		DebugAssert(citizenGenInfo.has_value());

		// Copy level definition directly into chunk.
		const WorldInt2 levelOffset = WorldInt2::Zero;
		this->populateChunkEntities(entityChunk, voxelChunk, levelDef, levelInfoDef, levelOffset, entityGenInfo,
			citizenGenInfo, random, entityDefLibrary, binaryAssetLibrary, textureManager, renderer);
	}
}

void EntityChunkManager::updateCitizenStates(double dt, EntityChunk &entityChunk, const CoordDouble2 &playerCoordXZ,
	bool isPlayerMoving, bool isPlayerWeaponSheathed, Random &random, const VoxelChunkManager &voxelChunkManager,
	const EntityDefinitionLibrary &entityDefLibrary)
{
	for (int i = static_cast<int>(entityChunk.entityIDs.size()) - 1; i >= 0; i--)
	{
		const EntityInstanceID entityInstID = entityChunk.entityIDs[i];
		const EntityInstance &entityInst = this->entities.get(entityInstID);
		if (!entityInst.isCitizen())
		{
			continue;
		}

		CoordDouble2 &entityCoord = this->positions.get(entityInst.positionID);
		const ChunkInt2 prevEntityChunkPos = entityCoord.chunk;
		const VoxelDouble2 dirToPlayer = playerCoordXZ - entityCoord;
		const double distToPlayerSqr = dirToPlayer.lengthSquared();

		const EntityDefinition &entityDef = this->getEntityDef(entityInst.defID, entityDefLibrary);
		const EntityAnimationDefinition &animDef = entityDef.getAnimDef();

		const std::optional<int> idleStateIndex = animDef.tryGetStateIndex(EntityAnimationUtils::STATE_IDLE.c_str());
		if (!idleStateIndex.has_value())
		{
			DebugCrash("Couldn't get citizen idle state index.");
		}

		const std::optional<int> walkStateIndex = animDef.tryGetStateIndex(EntityAnimationUtils::STATE_WALK.c_str());
		if (!walkStateIndex.has_value())
		{
			DebugCrash("Couldn't get citizen walk state index.");
		}

		EntityAnimationInstance &animInst = this->animInsts.get(entityInst.animInstID);
		VoxelDouble2 &entityDir = this->directions.get(entityInst.directionID);
		int8_t &citizenDirIndex = this->citizenDirectionIndices.get(entityInst.citizenDirectionIndexID);
		if (animInst.currentStateIndex == idleStateIndex)
		{
			const bool shouldChangeToWalking = !isPlayerWeaponSheathed || (distToPlayerSqr > CitizenUtils::IDLE_DISTANCE_SQR) || isPlayerMoving;

			// @todo: need to preserve their previous direction so they stay aligned with
			// the center of the voxel. Basically need to store cardinal direction as internal state.
			if (shouldChangeToWalking)
			{
				animInst.setStateIndex(*walkStateIndex);
				entityDir = CitizenUtils::getCitizenDirectionByIndex(citizenDirIndex);
			}
			else
			{
				// Face towards player.
				// @todo: cache the previous entity dir here so it can be popped when we return to walking. Could maybe have an EntityCitizenDirectionPool that stores ints.
				entityDir = dirToPlayer;
			}
		}
		else if (animInst.currentStateIndex == walkStateIndex)
		{
			const bool shouldChangeToIdle = isPlayerWeaponSheathed && (distToPlayerSqr <= CitizenUtils::IDLE_DISTANCE_SQR) && !isPlayerMoving;
			if (shouldChangeToIdle)
			{
				animInst.setStateIndex(*idleStateIndex);
			}
		}

		// Update citizen position and change facing if about to hit something.
		const int curAnimStateIndex = animInst.currentStateIndex;
		if (curAnimStateIndex == *walkStateIndex)
		{
			auto getVoxelAtDistance = [&entityCoord](const VoxelDouble2 &checkDist) -> CoordInt2
			{
				const CoordDouble2 pos = entityCoord + checkDist;
				return CoordInt2(pos.chunk, VoxelUtils::pointToVoxel(pos.point));
			};

			const CoordInt2 curVoxel(entityCoord.chunk, VoxelUtils::pointToVoxel(entityCoord.point));
			const CoordInt2 nextVoxel = getVoxelAtDistance(entityDir * 0.50);

			if (nextVoxel != curVoxel)
			{
				auto isSuitableVoxel = [&voxelChunkManager](const CoordInt2 &coord)
				{
					const VoxelChunk *voxelChunk = voxelChunkManager.tryGetChunkAtPosition(coord.chunk);

					auto isValidVoxel = [voxelChunk]()
					{
						return voxelChunk != nullptr;
					};

					auto isPassableVoxel = [&coord, voxelChunk]()
					{
						const VoxelInt3 voxel(coord.voxel.x, 1, coord.voxel.y);
						const VoxelChunk::VoxelTraitsDefID voxelTraitsDefID = voxelChunk->getTraitsDefID(voxel.x, voxel.y, voxel.z);
						const VoxelTraitsDefinition &voxelTraitsDef = voxelChunk->getTraitsDef(voxelTraitsDefID);
						return voxelTraitsDef.type == ArenaTypes::VoxelType::None;
					};

					auto isWalkableVoxel = [&coord, voxelChunk]()
					{
						const VoxelInt3 voxel(coord.voxel.x, 0, coord.voxel.y);
						const VoxelChunk::VoxelTraitsDefID voxelTraitsDefID = voxelChunk->getTraitsDefID(voxel.x, voxel.y, voxel.z);
						const VoxelTraitsDefinition &voxelTraitsDef = voxelChunk->getTraitsDef(voxelTraitsDefID);
						return voxelTraitsDef.type == ArenaTypes::VoxelType::Floor;
					};

					return isValidVoxel() && isPassableVoxel() && isWalkableVoxel();
				};

				if (!isSuitableVoxel(nextVoxel))
				{
					// Need to change walking direction. Determine another safe route, or if
					// none exist, then stop walking.
					const CardinalDirectionName curDirectionName = CardinalDirection::getDirectionName(entityDir);

					// Shuffle citizen direction indices so they don't all switch to the same direction every time.
					constexpr auto &dirIndices = CitizenUtils::DIRECTION_INDICES;
					int8_t randomDirectionIndices[std::size(dirIndices)];
					std::copy(std::begin(dirIndices), std::end(dirIndices), std::begin(randomDirectionIndices));
					RandomUtils::shuffle<int8_t>(randomDirectionIndices, random);

					const int8_t *indicesBegin = std::begin(randomDirectionIndices);
					const int8_t *indicesEnd = std::end(randomDirectionIndices);
					const auto iter = std::find_if(indicesBegin, indicesEnd,
						[&getVoxelAtDistance, &isSuitableVoxel, curDirectionName](int8_t dirIndex)
					{
						// See if this is a valid direction to go in.
						const CardinalDirectionName cardinalDirectionName = CitizenUtils::getCitizenDirectionNameByIndex(dirIndex);
						if (cardinalDirectionName != curDirectionName)
						{
							const WorldDouble2 &direction = CitizenUtils::getCitizenDirectionByIndex(dirIndex);
							const CoordInt2 voxel = getVoxelAtDistance(direction * 0.50);
							if (isSuitableVoxel(voxel))
							{
								return true;
							}
						}

						return false;
					});

					if (iter != indicesEnd)
					{
						citizenDirIndex = *iter;
						entityDir = CitizenUtils::getCitizenDirectionByIndex(citizenDirIndex);
					}
					else
					{
						// Couldn't find any valid direction. The citizen is probably stuck somewhere.
					}
				}
			}

			// Integrate by delta time.
			const VoxelDouble2 entityVelocity = entityDir * CitizenUtils::SPEED;
			entityCoord = ChunkUtils::recalculateCoord(entityCoord.chunk, entityCoord.point + (entityVelocity * dt));
		}

		// Transfer ownership of the entity ID to a new chunk if needed.
		const ChunkInt2 curEntityChunkPos = entityCoord.chunk;
		if (curEntityChunkPos != prevEntityChunkPos)
		{
			EntityChunk &curEntityChunk = this->getChunkAtPosition(curEntityChunkPos);
			entityChunk.entityIDs.erase(entityChunk.entityIDs.begin() + i);
			entityChunk.removedEntityIDs.emplace_back(entityInstID);
			curEntityChunk.entityIDs.emplace_back(entityInstID);
			curEntityChunk.addedEntityIDs.emplace_back(entityInstID);
		}
	}
}

std::string EntityChunkManager::getCreatureSoundFilename(const EntityDefID defID, const EntityDefinitionLibrary &entityDefLibrary) const
{
	const EntityDefinition &entityDef = this->getEntityDef(defID, entityDefLibrary);
	if (entityDef.getType() != EntityDefinition::Type::Enemy)
	{
		return std::string();
	}

	const auto &enemyDef = entityDef.getEnemy();
	if (enemyDef.getType() != EntityDefinition::EnemyDefinition::Type::Creature)
	{
		return std::string();
	}

	const auto &creatureDef = enemyDef.getCreature();
	const std::string_view creatureSoundName = creatureDef.soundName;
	return String::toUppercase(std::string(creatureSoundName));
}

const EntityInstance &EntityChunkManager::getEntity(EntityInstanceID id) const
{
	return this->entities.get(id);
}

const CoordDouble2 &EntityChunkManager::getEntityPosition(EntityPositionID id) const
{
	return this->positions.get(id);
}

double EntityChunkManager::getEntityBoundingBox(EntityBoundingBoxID id) const
{
	return this->boundingBoxes.get(id);
}

const VoxelDouble2 &EntityChunkManager::getEntityDirection(EntityDirectionID id) const
{
	return this->directions.get(id);
}

const EntityAnimationInstance &EntityChunkManager::getEntityAnimationInstance(EntityAnimationInstanceID id) const
{
	return this->animInsts.get(id);
}

const int8_t &EntityChunkManager::getEntityCitizenDirectionIndex(EntityCitizenDirectionIndexID id) const
{
	return this->citizenDirectionIndices.get(id);
}

const Palette &EntityChunkManager::getEntityPalette(EntityPaletteInstanceID id) const
{
	return this->palettes.get(id);
}

int EntityChunkManager::getCountInChunkWithDirection(const ChunkInt2 &chunkPos) const
{
	int count = 0;
	const std::optional<int> chunkIndex = this->tryGetChunkIndex(chunkPos);
	if (!chunkIndex.has_value())
	{
		DebugLogWarning("Missing chunk (" + chunkPos.toString() + ") for counting entities with direction.");
		return 0;
	}

	const EntityChunk &chunk = this->getChunkAtIndex(*chunkIndex);
	for (const EntityInstanceID entityInstID : chunk.entityIDs)
	{
		const EntityInstance &entityInst = this->entities.get(entityInstID);
		if (entityInst.directionID >= 0)
		{
			count++;
		}
	}

	return count;
}

int EntityChunkManager::getCountInChunkWithCreatureSound(const ChunkInt2 &chunkPos) const
{
	int count = 0;
	const std::optional<int> chunkIndex = this->tryGetChunkIndex(chunkPos);
	if (!chunkIndex.has_value())
	{
		DebugLogWarning("Missing chunk (" + chunkPos.toString() + ") for counting entities with creature sound.");
		return 0;
	}

	const EntityChunk &chunk = this->getChunkAtIndex(*chunkIndex);
	for (const EntityInstanceID entityInstID : chunk.entityIDs)
	{
		const EntityInstance &entityInst = this->entities.get(entityInstID);
		if (entityInst.creatureSoundInstID >= 0)
		{
			count++;
		}
	}

	return count;
}

int EntityChunkManager::getCountInChunkWithCitizenDirection(const ChunkInt2 &chunkPos) const
{
	int count = 0;
	const std::optional<int> chunkIndex = this->tryGetChunkIndex(chunkPos);
	if (!chunkIndex.has_value())
	{
		DebugLogWarning("Missing chunk (" + chunkPos.toString() + ") for counting entities with citizen direction.");
		return 0;
	}

	const EntityChunk &chunk = this->getChunkAtIndex(*chunkIndex);
	for (const EntityInstanceID entityInstID : chunk.entityIDs)
	{
		const EntityInstance &entityInst = this->entities.get(entityInstID);
		if (entityInst.citizenDirectionIndexID >= 0)
		{
			count++;
		}
	}

	return count;
}

BufferView<const EntityInstanceID> EntityChunkManager::getQueuedDestroyEntityIDs() const
{
	return this->destroyedEntityIDs;
}

void EntityChunkManager::getEntityVisibilityState2D(EntityInstanceID id, const CoordDouble2 &eye2D,
	const EntityDefinitionLibrary &entityDefLibrary, EntityVisibilityState2D &outVisState) const
{
	const EntityInstance &entityInst = this->entities.get(id);
	const EntityDefinition &entityDef = this->getEntityDef(entityInst.defID, entityDefLibrary);
	const EntityAnimationDefinition &animDef = entityDef.getAnimDef();
	const EntityAnimationInstance &animInst = this->animInsts.get(entityInst.animInstID);

	const CoordDouble2 &entityCoord = this->positions.get(entityInst.positionID);
	const bool isDynamic = entityInst.isDynamic();

	// Get active animation state.
	const int stateIndex = animInst.currentStateIndex;
	DebugAssert(stateIndex >= 0);
	DebugAssert(stateIndex < animDef.stateCount);
	const EntityAnimationDefinitionState &animDefState = animDef.states[stateIndex];

	// Get animation angle based on entity direction relative to some camera/eye.
	const int angleCount = animDefState.keyframeListCount;
	const Radians animAngle = [this, &eye2D, &entityInst, &entityCoord, isDynamic, angleCount]()
	{
		if (!isDynamic)
		{
			// Static entities always face the camera.
			return 0.0;
		}
		else
		{
			const VoxelDouble2 &entityDir = this->getEntityDirection(entityInst.directionID);

			// Dynamic entities are angle-dependent.
			const VoxelDouble2 diffDir = (eye2D - entityCoord).normalized();

			const Radians entityAngle = MathUtils::fullAtan2(entityDir);
			const Radians diffAngle = MathUtils::fullAtan2(diffDir);

			// Use the difference of the two angles to get the relative angle.
			const Radians resultAngle = Constants::TwoPi + (entityAngle - diffAngle);

			// Angle bias so the final direction is centered within its angle range.
			const Radians angleBias = (Constants::TwoPi / static_cast<double>(angleCount)) * 0.50;

			return std::fmod(resultAngle + angleBias, Constants::TwoPi);
		}
	}();

	// Index into animation keyframe lists for the state.
	const int angleIndex = [angleCount, animAngle]()
	{
		const double angleCountReal = static_cast<double>(angleCount);
		const double anglePercent = animAngle / Constants::TwoPi;
		const int angleIndex = static_cast<int>(angleCountReal * anglePercent);
		return std::clamp(angleIndex, 0, angleCount - 1);
	}();

	// Keyframe list for the current state and angle.
	const int animDefKeyframeListIndex = animDefState.keyframeListsIndex + angleIndex;
	DebugAssert(animDefKeyframeListIndex >= 0);
	DebugAssert(animDefKeyframeListIndex < animDef.keyframeListCount);
	const EntityAnimationDefinitionKeyframeList &animDefKeyframeList = animDef.keyframeLists[animDefKeyframeListIndex];

	// Progress through current animation.
	const int keyframeIndex = [&animInst, &animDefState, &animDefKeyframeList]()
	{
		const int keyframeCount = animDefKeyframeList.keyframeCount;
		const double keyframeCountReal = static_cast<double>(keyframeCount);
		const double animPercent = animInst.progressPercent;
		const int keyframeIndex = static_cast<int>(keyframeCountReal * animPercent);
		return std::clamp(keyframeIndex, 0, keyframeCount - 1);
	}();

	const CoordDouble2 flatPosition(
		entityCoord.chunk,
		VoxelDouble2(entityCoord.point.x, entityCoord.point.y));

	outVisState.init(id, flatPosition, stateIndex, angleIndex, keyframeIndex);
}

void EntityChunkManager::getEntityVisibilityState3D(EntityInstanceID id, const CoordDouble2 &eye2D,
	double ceilingScale, const VoxelChunkManager &voxelChunkManager, const EntityDefinitionLibrary &entityDefLibrary,
	EntityVisibilityState3D &outVisState) const
{
	EntityVisibilityState2D visState2D;
	this->getEntityVisibilityState2D(id, eye2D, entityDefLibrary, visState2D);

	const EntityInstance &entityInst = this->entities.get(id);
	const EntityDefinition &entityDef = this->getEntityDef(entityInst.defID, entityDefLibrary);
	const int baseYOffset = EntityUtils::getYOffset(entityDef);
	const double flatYOffset = static_cast<double>(-baseYOffset) / MIFUtils::ARENA_UNITS;

	// If the entity is in a raised platform voxel, they are set on top of it.
	const double raisedPlatformYOffset = [ceilingScale, &voxelChunkManager, &visState2D]()
	{
		const CoordInt2 entityVoxelCoord(
			visState2D.flatPosition.chunk,
			VoxelUtils::pointToVoxel(visState2D.flatPosition.point));
		const VoxelChunk *chunk = voxelChunkManager.tryGetChunkAtPosition(entityVoxelCoord.chunk);
		if (chunk == nullptr)
		{
			// Not sure this is ever reachable, but handle just in case.
			return 0.0;
		}

		const VoxelChunk::VoxelTraitsDefID voxelTraitsDefID = chunk->getTraitsDefID(entityVoxelCoord.voxel.x, 1, entityVoxelCoord.voxel.y);
		const VoxelTraitsDefinition &voxelTraitsDef = chunk->getTraitsDef(voxelTraitsDefID);

		if (voxelTraitsDef.type == ArenaTypes::VoxelType::Raised)
		{
			const VoxelTraitsDefinition::Raised &raised = voxelTraitsDef.raised;
			const double meshYPos = raised.yOffset + raised.ySize;

			const VoxelChunk::VoxelMeshDefID voxelMeshDefID = chunk->getMeshDefID(entityVoxelCoord.voxel.x, 1, entityVoxelCoord.voxel.y);
			const VoxelMeshDefinition &voxelMeshDef = chunk->getMeshDef(voxelMeshDefID);

			return MeshUtils::getScaledVertexY(meshYPos, voxelMeshDef.scaleType, ceilingScale);
		}
		else
		{
			// No raised platform offset.
			return 0.0;
		}
	}();

	// Bottom center of flat.
	const VoxelDouble3 flatPoint(
		visState2D.flatPosition.point.x,
		ceilingScale + flatYOffset + raisedPlatformYOffset,
		visState2D.flatPosition.point.y);
	const CoordDouble3 flatPosition(visState2D.flatPosition.chunk, flatPoint);

	outVisState.init(id, flatPosition, visState2D.stateIndex, visState2D.angleIndex, visState2D.keyframeIndex);
}

void EntityChunkManager::updateCreatureSounds(double dt, EntityChunk &entityChunk, const CoordDouble3 &playerCoord,
	double ceilingScale, Random &random, const EntityDefinitionLibrary &entityDefLibrary, AudioManager &audioManager)
{
	const int entityCount = static_cast<int>(entityChunk.entityIDs.size());
	for (int i = 0; i < entityCount; i++)
	{
		const EntityInstanceID instID = entityChunk.entityIDs[i];
		EntityInstance &entityInst = this->entities.get(instID);
		if (entityInst.creatureSoundInstID >= 0)
		{
			double &secondsTillCreatureSound = this->creatureSoundInsts.get(entityInst.creatureSoundInstID);
			secondsTillCreatureSound -= dt;
			if (secondsTillCreatureSound <= 0.0)
			{
				const CoordDouble2 &entityCoord = this->positions.get(entityInst.positionID);
				if (EntityUtils::withinHearingDistance(playerCoord, entityCoord, ceilingScale))
				{
					// @todo: store some kind of sound def ID w/ the secondsTillCreatureSound instead of generating the sound filename here.
					const std::string creatureSoundFilename = this->getCreatureSoundFilename(entityInst.defID, entityDefLibrary);
					if (creatureSoundFilename.empty())
					{
						continue;
					}

					// Center the sound inside the creature.
					const CoordDouble3 soundCoord(
						entityCoord.chunk,
						VoxelDouble3(entityCoord.point.x, ceilingScale * 1.50, entityCoord.point.y));
					const WorldDouble3 absoluteSoundPosition = VoxelUtils::coordToWorldPoint(soundCoord);
					audioManager.playSound(creatureSoundFilename, absoluteSoundPosition);

					secondsTillCreatureSound = EntityUtils::nextCreatureSoundWaitTime(random);
				}
			}
		}
	}
}

void EntityChunkManager::update(double dt, const BufferView<const ChunkInt2> &activeChunkPositions,
	const BufferView<const ChunkInt2> &newChunkPositions, const BufferView<const ChunkInt2> &freedChunkPositions,
	const Player &player, const LevelDefinition *activeLevelDef, const LevelInfoDefinition *activeLevelInfoDef,
	const MapSubDefinition &mapSubDef, BufferView<const LevelDefinition> levelDefs,
	BufferView<const int> levelInfoDefIndices, BufferView<const LevelInfoDefinition> levelInfoDefs,
	const EntityGeneration::EntityGenInfo &entityGenInfo, const std::optional<CitizenUtils::CitizenGenInfo> &citizenGenInfo,
	double ceilingScale, Random &random, const VoxelChunkManager &voxelChunkManager, AudioManager &audioManager,
	TextureManager &textureManager, Renderer &renderer)
{
	const EntityDefinitionLibrary &entityDefLibrary = EntityDefinitionLibrary::getInstance();
	const BinaryAssetLibrary &binaryAssetLibrary = BinaryAssetLibrary::getInstance();

	for (const ChunkInt2 &chunkPos : freedChunkPositions)
	{
		const int chunkIndex = this->getChunkIndex(chunkPos);
		const EntityChunk &entityChunk = this->getChunkAtIndex(chunkIndex);
		for (const EntityInstanceID entityInstID : entityChunk.entityIDs)
		{
			this->queueEntityDestroy(entityInstID);
		}

		this->recycleChunk(chunkIndex);
	}

	const MapType mapType = mapSubDef.type;
	for (const ChunkInt2 &chunkPos : newChunkPositions)
	{
		const VoxelChunk &voxelChunk = voxelChunkManager.getChunkAtPosition(chunkPos);
		const int spawnIndex = this->spawnChunk();
		EntityChunk &entityChunk = this->getChunkAtIndex(spawnIndex);
		entityChunk.init(chunkPos, voxelChunk.getHeight());

		// Default to the active level def unless it's the wilderness which relies on this chunk coordinate.
		const LevelDefinition *levelDefPtr = activeLevelDef;
		const LevelInfoDefinition *levelInfoDefPtr = activeLevelInfoDef;
		if (mapType == MapType::Wilderness)
		{
			const MapDefinitionWild &mapDefWild = mapSubDef.wild;
			const int levelDefIndex = mapDefWild.getLevelDefIndex(chunkPos);
			levelDefPtr = &levelDefs[levelDefIndex];

			const int levelInfoDefIndex = levelInfoDefIndices[levelDefIndex];
			levelInfoDefPtr = &levelInfoDefs[levelInfoDefIndex];
		}

		this->populateChunk(entityChunk, voxelChunk, *levelDefPtr, *levelInfoDefPtr, mapSubDef,
			entityGenInfo, citizenGenInfo, ceilingScale, random, entityDefLibrary, binaryAssetLibrary,
			textureManager, renderer);
	}

	// Free any unneeded chunks for memory savings in case the chunk distance was once large
	// and is now small. This is significant even for chunk distance 2->1, or 25->9 chunks.
	this->chunkPool.clear();

	const CoordDouble3 &playerCoord = player.getPosition();
	const CoordDouble2 playerCoordXZ(playerCoord.chunk, VoxelDouble2(playerCoord.point.x, playerCoord.point.z));
	const bool isPlayerMoving = player.getVelocity().lengthSquared() >= Constants::Epsilon;
	const bool isPlayerWeaponSheathed = player.getWeaponAnimation().isSheathed();

	for (const ChunkInt2 &chunkPos : activeChunkPositions)
	{
		const int chunkIndex = this->getChunkIndex(chunkPos);
		EntityChunk &entityChunk = this->getChunkAtIndex(chunkIndex);
		const VoxelChunk &voxelChunk = voxelChunkManager.getChunkAtPosition(chunkPos);

		// @todo: simulate/animate AI
		this->updateCitizenStates(dt, entityChunk, playerCoordXZ, isPlayerMoving, isPlayerWeaponSheathed, random,
			voxelChunkManager, entityDefLibrary);

		for (const EntityInstanceID entityInstID : entityChunk.entityIDs)
		{
			const EntityInstance &entityInst = this->entities.get(entityInstID);
			EntityAnimationInstance &animInst = this->animInsts.get(entityInst.animInstID);
			animInst.update(dt);
		}

		this->updateCreatureSounds(dt, entityChunk, playerCoord, ceilingScale, random, entityDefLibrary, audioManager);
	}
}

void EntityChunkManager::queueEntityDestroy(EntityInstanceID entityInstID)
{
	const auto iter = std::find(this->destroyedEntityIDs.begin(), this->destroyedEntityIDs.end(), entityInstID);
	if (iter == this->destroyedEntityIDs.end())
	{
		this->destroyedEntityIDs.emplace_back(entityInstID);
	}
}

void EntityChunkManager::cleanUp()
{
	for (const EntityInstanceID entityInstID : this->destroyedEntityIDs)
	{
		const EntityInstance &entityInst = this->entities.get(entityInstID);
		
		if (entityInst.positionID >= 0)
		{
			this->positions.free(entityInst.positionID);
		}

		if (entityInst.bboxID >= 0)
		{
			this->boundingBoxes.free(entityInst.bboxID);
		}

		if (entityInst.directionID >= 0)
		{
			this->directions.free(entityInst.directionID);
		}

		if (entityInst.animInstID >= 0)
		{
			this->animInsts.free(entityInst.animInstID);
		}

		if (entityInst.creatureSoundInstID >= 0)
		{
			this->creatureSoundInsts.free(entityInst.creatureSoundInstID);
		}

		if (entityInst.citizenDirectionIndexID >= 0)
		{
			this->citizenDirectionIndices.free(entityInst.citizenDirectionIndexID);
		}

		if (entityInst.paletteInstID >= 0)
		{
			this->palettes.free(entityInst.paletteInstID);
		}
		
		this->entities.free(entityInstID);
	}

	this->destroyedEntityIDs.clear();

	for (ChunkPtr &chunkPtr : this->activeChunks)
	{
		chunkPtr->addedEntityIDs.clear();
		chunkPtr->removedEntityIDs.clear();
	}
}

void EntityChunkManager::clear()
{
	for (ChunkPtr &chunkPtr : this->activeChunks)
	{
		for (const EntityInstanceID entityInstID : chunkPtr->entityIDs)
		{
			this->queueEntityDestroy(entityInstID);
		}
	}

	this->cleanUp();
	this->recycleAllChunks();
}
