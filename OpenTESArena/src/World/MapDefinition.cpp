#include <unordered_map>

#include "CityWorldUtils.h"
#include "InteriorWorldUtils.h"
#include "MapDefinition.h"
#include "MapGeneration.h"
#include "WorldType.h"
#include "WildWorldUtils.h"
#include "../Assets/BinaryAssetLibrary.h"
#include "../Assets/MIFFile.h"
#include "../Assets/MIFUtils.h"
#include "../Assets/RMDFile.h"
#include "../Math/Random.h"

#include "components/debug/Debug.h"
#include "components/utilities/BufferView.h"
#include "components/utilities/String.h"

void MapDefinition::InteriorGenerationInfo::Interior::init(std::string &&mifName, std::string &&displayName,
	bool isPalace, const std::optional<bool> &rulerIsMale)
{
	this->mifName = std::move(mifName);
	this->displayName = std::move(displayName);
	this->isPalace = isPalace;
	this->rulerIsMale = rulerIsMale;
}

void MapDefinition::InteriorGenerationInfo::Dungeon::init(uint32_t dungeonSeed, WEInt widthChunks,
	SNInt depthChunks, bool isArtifactDungeon)
{
	this->dungeonSeed = dungeonSeed;
	this->widthChunks = widthChunks;
	this->depthChunks = depthChunks;
	this->isArtifactDungeon = isArtifactDungeon;
}

MapDefinition::InteriorGenerationInfo::InteriorGenerationInfo()
{
	this->type = static_cast<InteriorGenerationInfo::Type>(-1);
}

void MapDefinition::InteriorGenerationInfo::init(Type type)
{
	this->type = type;
}

void MapDefinition::InteriorGenerationInfo::initInterior(std::string &&mifName, std::string &&displayName,
	bool isPalace, const std::optional<bool> &rulerIsMale)
{
	this->init(InteriorGenerationInfo::Type::Interior);
	this->interior.init(std::move(mifName), std::move(displayName), isPalace, rulerIsMale);
}

void MapDefinition::InteriorGenerationInfo::initDungeon(uint32_t dungeonSeed, WEInt widthChunks,
	SNInt depthChunks, bool isArtifactDungeon)
{
	this->init(InteriorGenerationInfo::Type::Dungeon);
	this->dungeon.init(dungeonSeed, widthChunks, depthChunks, isArtifactDungeon);
}

MapDefinition::InteriorGenerationInfo::Type MapDefinition::InteriorGenerationInfo::getType() const
{
	return this->type;
}

const MapDefinition::InteriorGenerationInfo::Interior &MapDefinition::InteriorGenerationInfo::getInterior() const
{
	DebugAssert(this->type == InteriorGenerationInfo::Type::Interior);
	return this->interior;
}

const MapDefinition::InteriorGenerationInfo::Dungeon &MapDefinition::InteriorGenerationInfo::getDungeon() const
{
	DebugAssert(this->type == InteriorGenerationInfo::Type::Dungeon);
	return this->dungeon;
}

void MapDefinition::CityGenerationInfo::init(std::string &&mifName, uint32_t citySeed, bool isPremade,
	Buffer<uint8_t> &&reservedBlocks, WEInt blockStartPosX, SNInt blockStartPosY,
	int cityBlocksPerSide)
{
	this->mifName = std::move(mifName);
	this->citySeed = citySeed;
	this->isPremade = isPremade;
	this->reservedBlocks = std::move(reservedBlocks);
	this->blockStartPosX = blockStartPosX;
	this->blockStartPosY = blockStartPosY;
	this->cityBlocksPerSide = cityBlocksPerSide;
}

void MapDefinition::WildGenerationInfo::init(Buffer2D<WildBlockID> &&wildBlockIDs, uint32_t fallbackSeed)
{
	this->wildBlockIDs = std::move(wildBlockIDs);
	this->fallbackSeed = fallbackSeed;
}

void MapDefinition::Interior::init()
{
	
}

const MapDefinition::InteriorGenerationInfo &MapDefinition::City::getInteriorGenerationInfo(int index) const
{
	DebugAssertIndex(this->interiorGenInfos, index);
	return this->interiorGenInfos[index];
}

int MapDefinition::City::addInteriorGenerationInfo(InteriorGenerationInfo &&generationInfo)
{
	this->interiorGenInfos.emplace_back(std::move(generationInfo));
	return static_cast<int>(this->interiorGenInfos.size()) - 1;
}

void MapDefinition::Wild::init(Buffer2D<int> &&levelDefIndices, uint32_t fallbackSeed)
{
	this->levelDefIndices = std::move(levelDefIndices);
	this->fallbackSeed = fallbackSeed;
}

int MapDefinition::Wild::getLevelDefIndex(const ChunkInt2 &chunk) const
{
	const bool isValid = (chunk.x >= 0) && (chunk.x < this->levelDefIndices.getWidth()) &&
		(chunk.y >= 0) && (chunk.y < this->levelDefIndices.getHeight());

	if (isValid)
	{
		return this->levelDefIndices.get(chunk.x, chunk.y);
	}
	else
	{
		// @todo: use fallbackSeed when outside the defined wild chunks. Not sure yet how
		// to generate a random value between 0 and levelDefCount for an arbitrary Int2
		// without running random.next() some arbitrary number of times.
		return 0;
	}
}

const MapDefinition::InteriorGenerationInfo &MapDefinition::Wild::getInteriorGenerationInfo(int index) const
{
	DebugAssertIndex(this->interiorGenInfos, index);
	return this->interiorGenInfos[index];
}

int MapDefinition::Wild::addInteriorGenerationInfo(InteriorGenerationInfo &&generationInfo)
{
	this->interiorGenInfos.emplace_back(std::move(generationInfo));
	return static_cast<int>(this->interiorGenInfos.size()) - 1;
}

void MapDefinition::init(WorldType worldType)
{
	this->worldType = worldType;
}

bool MapDefinition::initInteriorLevels(const MIFFile &mif, bool isPalace,
	const std::optional<bool> &rulerIsMale, const CharacterClassLibrary &charClassLibrary,
	const EntityDefinitionLibrary &entityDefLibrary, const BinaryAssetLibrary &binaryAssetLibrary,
	TextureManager &textureManager)
{
	// N level + level info pairs.
	this->levels.init(mif.getLevelCount());
	this->levelInfos.init(mif.getLevelCount());
	this->levelInfoMappings.init(mif.getLevelCount());

	auto initLevelAndInfo = [this, &mif, isPalace, &rulerIsMale, &charClassLibrary, &entityDefLibrary,
		&binaryAssetLibrary, &textureManager](int levelIndex, const MIFFile::Level &mifLevel,
		const INFFile &inf)
	{
		LevelDefinition &levelDef = this->levels.get(levelIndex);
		LevelInfoDefinition &levelInfoDef = this->levelInfos.get(levelIndex);

		const INFFile::CeilingData &ceiling = inf.getCeiling();
		const WEInt levelWidth = mif.getWidth();
		const int levelHeight = LevelUtils::getMifLevelHeight(mifLevel, &ceiling);
		const SNInt levelDepth = mif.getDepth();

		// Transpose .MIF dimensions to new dimensions.
		levelDef.init(levelDepth, levelHeight, levelWidth);

		const double ceilingScale = LevelUtils::convertArenaCeilingHeight(ceiling.height);
		levelInfoDef.init(ceilingScale);

		// Set LevelDefinition and LevelInfoDefinition voxels and entities from .MIF + .INF together
		// (due to ceiling, etc.).
		const BufferView<const MIFFile::Level> mifLevelView(&mifLevel, 1);
		const WorldType worldType = WorldType::Interior;
		BufferView<LevelDefinition> levelDefView(&levelDef, 1);
		MapGeneration::readMifVoxels(mifLevelView, worldType, isPalace, rulerIsMale, inf, charClassLibrary,
			entityDefLibrary, binaryAssetLibrary, textureManager, levelDefView, &levelInfoDef);
		MapGeneration::readMifLocks(mifLevelView, inf, levelDefView, &levelInfoDef);
		MapGeneration::readMifTriggers(mifLevelView, inf, levelDefView, &levelInfoDef);
	};

	for (int i = 0; i < mif.getLevelCount(); i++)
	{
		const MIFFile::Level &level = mif.getLevel(i);
		const std::string infName = String::toUppercase(level.getInfo());
		INFFile inf;
		if (!inf.init(infName.c_str()))
		{
			DebugLogError("Couldn't init .INF file \"" + infName + "\".");
			return false;
		}

		initLevelAndInfo(i, level, inf);
	}

	// Each interior level info maps to its parallel level.
	for (int i = 0; i < this->levelInfoMappings.getCount(); i++)
	{
		this->levelInfoMappings.set(i, i);
	}

	return true;
}

bool MapDefinition::initDungeonLevels(const MIFFile &mif, WEInt widthChunks, SNInt depthChunks,
	bool isArtifactDungeon, ArenaRandom &random, const CharacterClassLibrary &charClassLibrary,
	const EntityDefinitionLibrary &entityDefLibrary, const BinaryAssetLibrary &binaryAssetLibrary,
	TextureManager &textureManager, LevelInt2 *outStartPoint)
{
	const int levelCount = InteriorWorldUtils::generateDungeonLevelCount(isArtifactDungeon, random);

	// N LevelDefinitions all pointing to one LevelInfoDefinition.
	this->levels.init(levelCount);
	this->levelInfos.init(1);
	this->levelInfoMappings.init(levelCount);

	// Use the .INF filename of the first level.
	const MIFFile::Level &level = mif.getLevel(0);
	const std::string infName = String::toUppercase(level.getInfo());
	INFFile inf;
	if (!inf.init(infName.c_str()))
	{
		DebugLogError("Couldn't init .INF file \"" + infName + "\".");
		return false;
	}

	const INFFile::CeilingData &ceiling = inf.getCeiling();
	const WEInt levelWidth = mif.getWidth() * widthChunks;
	const int levelHeight = ceiling.outdoorDungeon ? 2 : 3;
	const SNInt levelDepth = mif.getDepth() * depthChunks;

	for (int i = 0; i < this->levels.getCount(); i++)
	{
		// Transpose .MIF dimensions to new dimensions.
		LevelDefinition &levelDef = this->levels.get(i);
		levelDef.init(levelDepth, levelHeight, levelWidth);
	}

	BufferView<LevelDefinition> levelDefView(this->levels.get(), this->levels.getCount());
	LevelInfoDefinition &levelInfoDef = this->levelInfos.get(0);

	const double ceilingScale = LevelUtils::convertArenaCeilingHeight(ceiling.height);
	levelInfoDef.init(ceilingScale);

	constexpr bool isPalace = false;
	constexpr std::optional<bool> rulerIsMale;
	MapGeneration::generateMifDungeon(mif, levelCount, widthChunks, depthChunks, inf, random,
		worldType, isPalace, rulerIsMale, charClassLibrary, entityDefLibrary, binaryAssetLibrary,
		textureManager, levelDefView, &levelInfoDef, outStartPoint);

	// Each dungeon level uses the same level info definition.
	for (int i = 0; i < this->levelInfoMappings.getCount(); i++)
	{
		this->levelInfoMappings.set(i, 0);
	}

	return true;
}

bool MapDefinition::initCityLevel(const MIFFile &mif, uint32_t citySeed, bool isPremade,
	const BufferView<const uint8_t> &reservedBlocks, WEInt blockStartPosX, SNInt blockStartPosY,
	int cityBlocksPerSide, const INFFile &inf, const CharacterClassLibrary &charClassLibrary,
	const EntityDefinitionLibrary &entityDefLibrary, const BinaryAssetLibrary &binaryAssetLibrary,
	TextureManager &textureManager)
{
	// 1 LevelDefinition and 1 LevelInfoDefinition.
	this->levels.init(1);
	this->levelInfos.init(1);
	this->levelInfoMappings.init(1);

	const WEInt levelWidth = mif.getWidth();
	const int levelHeight =  6;
	const SNInt levelDepth = mif.getDepth();

	// Transpose .MIF dimensions to new dimensions.
	LevelDefinition &levelDef = this->levels.get(0);
	levelDef.init(levelDepth, levelHeight, levelWidth);

	const INFFile::CeilingData &ceiling = inf.getCeiling();
	const double ceilingScale = LevelUtils::convertArenaCeilingHeight(ceiling.height);
	LevelInfoDefinition &levelInfoDef = this->levelInfos.get(0);
	levelInfoDef.init(ceilingScale);

	MapGeneration::generateMifCity(mif, citySeed, isPremade, reservedBlocks, blockStartPosX,
		blockStartPosY, cityBlocksPerSide, inf, charClassLibrary, entityDefLibrary, binaryAssetLibrary,
		textureManager, &levelDef, &levelInfoDef, &this->city);

	// Only one level info to use.
	this->levelInfoMappings.set(0, 0);
	
	return true;
}

void MapDefinition::initStartPoints(const MIFFile &mif)
{
	this->startPoints.init(mif.getStartPointCount());
	for (int i = 0; i < mif.getStartPointCount(); i++)
	{
		const OriginalInt2 &mifStartPoint = mif.getStartPoint(i);
		const Double2 mifStartPointReal = MIFUtils::convertStartPointToReal(mifStartPoint);
		this->startPoints.get(i) = VoxelUtils::getTransformedVoxel(mifStartPointReal);
	}
}

bool MapDefinition::initInterior(const InteriorGenerationInfo &generationInfo,
	const CharacterClassLibrary &charClassLibrary, const EntityDefinitionLibrary &entityDefLibrary,
	const BinaryAssetLibrary &binaryAssetLibrary, TextureManager &textureManager)
{
	this->init(WorldType::Interior);

	const InteriorGenerationInfo::Type interiorType = generationInfo.getType();
	if (interiorType == InteriorGenerationInfo::Type::Interior)
	{
		const InteriorGenerationInfo::Interior &interiorGenInfo = generationInfo.getInterior();
		MIFFile mif;
		if (!mif.init(interiorGenInfo.mifName.c_str()))
		{
			DebugLogError("Couldn't init .MIF file \"" + interiorGenInfo.mifName + "\".");
			return false;
		}

		this->initInteriorLevels(mif, interiorGenInfo.isPalace, interiorGenInfo.rulerIsMale,
			charClassLibrary, entityDefLibrary, binaryAssetLibrary, textureManager);
		this->initStartPoints(mif);
		this->startLevelIndex = mif.getStartingLevelIndex();
	}
	else if (interiorType == InteriorGenerationInfo::Type::Dungeon)
	{
		const InteriorGenerationInfo::Dungeon &dungeonGenInfo = generationInfo.getDungeon();

		// Dungeon .MIF file with chunks for random generation.
		const std::string &mifName = InteriorWorldUtils::DUNGEON_MIF_NAME;
		MIFFile mif;
		if (!mif.init(mifName.c_str()))
		{
			DebugLogError("Couldn't init .MIF file \"" + mifName + "\".");
			return false;
		}

		ArenaRandom random(dungeonGenInfo.dungeonSeed);

		// Generate dungeon levels and get the player start point.
		LevelInt2 startPoint;
		this->initDungeonLevels(mif, dungeonGenInfo.widthChunks, dungeonGenInfo.depthChunks,
			dungeonGenInfo.isArtifactDungeon, random, charClassLibrary, entityDefLibrary,
			binaryAssetLibrary, textureManager, &startPoint);

		const LevelDouble2 startPointReal(
			static_cast<SNDouble>(startPoint.x) + 0.50,
			static_cast<WEDouble>(startPoint.y) + 0.50);
		this->startPoints.init(1);
		this->startPoints.set(0, startPointReal);
		this->startLevelIndex = 0;
	}
	else
	{
		DebugCrash("Unrecognized interior generation type \"" +
			std::to_string(static_cast<int>(interiorType)) + "\".");
	}
	
	return true;
}

bool MapDefinition::initCity(const CityGenerationInfo &generationInfo, ClimateType climateType,
	WeatherType weatherType, const CharacterClassLibrary &charClassLibrary,
	const EntityDefinitionLibrary &entityDefLibrary, const BinaryAssetLibrary &binaryAssetLibrary,
	TextureManager &textureManager)
{
	this->init(WorldType::City);

	const std::string &mifName = generationInfo.mifName;
	MIFFile mif;
	if (!mif.init(mifName.c_str()))
	{
		DebugLogError("Couldn't init .MIF file \"" + mifName + "\".");
		return false;
	}

	const DOSUtils::FilenameBuffer infName = CityWorldUtils::generateInfName(climateType, weatherType);
	INFFile inf;
	if (!inf.init(infName.data()))
	{
		DebugLogError("Couldn't init .INF file \"" + std::string(infName.data()) + "\".");
		return false;
	}

	const BufferView<const uint8_t> reservedBlocks(generationInfo.reservedBlocks.get(),
		generationInfo.reservedBlocks.getCount());

	// Generate city level (optionally generating random city blocks if not premade).
	this->initCityLevel(mif, generationInfo.citySeed, generationInfo.isPremade, reservedBlocks,
		generationInfo.blockStartPosX, generationInfo.blockStartPosY, generationInfo.cityBlocksPerSide,
		inf, charClassLibrary, entityDefLibrary, binaryAssetLibrary, textureManager);
	this->initStartPoints(mif);
	this->startLevelIndex = 0;
	return true;
}

bool MapDefinition::initWild(const WildGenerationInfo &generationInfo, ClimateType climateType,
	WeatherType weatherType, const BinaryAssetLibrary &binaryAssetLibrary)
{
	this->init(WorldType::Wilderness);

	// @todo: 70 level definitions, 1 level info definition. Map all of them to 0

	/*auto initLevelDef = [this](LevelDefinition &levelDef, const RMDFile &rmd)
	{
		levelDef.initWild(rmd);
	};

	// Generate a level definition index for each unique wild block ID.
	std::unordered_map<WildBlockID, int> blockLevelDefMappings;
	for (int y = 0; y < wildBlockIDs.getHeight(); y++)
	{
		for (int x = 0; x < wildBlockIDs.getWidth(); x++)
		{
			const WildBlockID blockID = wildBlockIDs.get(x, y);
			const auto iter = blockLevelDefMappings.find(blockID);
			if (iter == blockLevelDefMappings.end())
			{
				const int levelDefIndex = static_cast<int>(blockLevelDefMappings.size());
				blockLevelDefMappings.emplace(blockID, levelDefIndex);
			}
		}
	}

	const int levelDefCount = static_cast<int>(blockLevelDefMappings.size());
	this->levels.init(levelDefCount);
	for (const auto &pair : blockLevelDefMappings)
	{
		const WildBlockID blockID = pair.first;
		const int levelDefIndex = pair.second;

		const RMDFile &rmd = [&binaryAssetLibrary, &wildBlockIDs, blockID]() -> const RMDFile&
		{
			const auto &rmdFiles = binaryAssetLibrary.getWildernessChunks();
			const int rmdIndex = DebugMakeIndex(rmdFiles, blockID - 1);
			return rmdFiles[rmdIndex];
		}();

		LevelDefinition &levelDef = this->levels.get(levelDefIndex);
		initLevelDef(levelDef, rmd);
	}

	const DOSUtils::FilenameBuffer infName = WildWorldUtils::generateInfName(climateType, weatherType);
	INFFile inf;
	if (!inf.init(infName.data()))
	{
		DebugLogError("Couldn't init .INF file \"" + std::string(infName.data()) + "\".");
		return false;
	}

	// Add level info pointed to by all wild chunks.
	LevelInfoDefinition levelInfoDef;
	levelInfoDef.init(inf);
	this->levelInfos.init(1);
	this->levelInfos.set(0, std::move(levelInfoDef));

	for (int i = 0; i < this->levels.getCount(); i++)
	{
		const int levelInfoIndex = 0;
		this->levelInfoMappings.push_back(levelInfoIndex);
	}*/

	DebugNotImplemented();
	return true;
}

const std::optional<int> &MapDefinition::getStartLevelIndex() const
{
	return this->startLevelIndex;
}

int MapDefinition::getStartPointCount() const
{
	return this->startPoints.getCount();
}

const LevelDouble2 &MapDefinition::getStartPoint(int index) const
{
	return this->startPoints.get(index);
}

int MapDefinition::getLevelCount() const
{
	return this->levels.getCount();
}

const LevelDefinition &MapDefinition::getLevel(int index) const
{
	return this->levels.get(index);
}

const LevelInfoDefinition &MapDefinition::getLevelInfoForLevel(int levelIndex) const
{
	const int levelInfoIndex = this->levelInfoMappings.get(levelIndex);
	return this->levelInfos.get(levelInfoIndex);
}

WorldType MapDefinition::getWorldType() const
{
	return this->worldType;
}

const MapDefinition::Interior &MapDefinition::getInterior() const
{
	DebugAssert(this->worldType == WorldType::Interior);
	return this->interior;
}

const MapDefinition::City &MapDefinition::getCity() const
{
	DebugAssert(this->worldType == WorldType::City);
	return this->city;
}

const MapDefinition::Wild &MapDefinition::getWild() const
{
	DebugAssert(this->worldType == WorldType::Wilderness);
	return this->wild;
}
