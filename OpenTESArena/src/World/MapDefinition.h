#ifndef MAP_DEFINITION_H
#define MAP_DEFINITION_H

#include <optional>
#include <string>
#include <vector>

#include "LevelDefinition.h"
#include "LevelInfoDefinition.h"
#include "VoxelUtils.h"
#include "WildLevelUtils.h"

#include "components/utilities/Buffer.h"
#include "components/utilities/BufferView2D.h"

// Modern replacement for .MIF/.RMD files. Helps create a buffer between how the game world data
// is defined and how it's represented in-engine, so that it doesn't care about things like
// chunks.

class ArenaRandom;
class BinaryAssetLibrary;
class CharacterClassLibrary;
class EntityDefinitionLibrary;
class TextureManager;

enum class ClimateType;
enum class WeatherType;
enum class WorldType;

class MapDefinition
{
public:
	// Data for generating an interior map (building interior, wild den, world map dungeon, etc.).
	class InteriorGenerationInfo
	{
	public:
		enum class Type { Interior, Dungeon };

		// Input: N .MIF levels + N level-referenced .INFs
		// Output: N LevelDefinition / LevelInfoDefinition pairs
		struct Interior
		{
			std::string mifName;
			std::string displayName; // For building interior transitions (tavern, temple, etc.).
			bool isPalace; // @todo: eventually change to something that supports all Arena interiors.
			std::optional<bool> rulerIsMale;

			void init(std::string &&mifName, std::string &&displayName, bool isPalace,
				const std::optional<bool> &rulerIsMale);
		};

		// Input: RANDOM1.MIF + RD1.INF (loaded internally) + seed + chunk dimensions
		// Output: N LevelDefinitions + 1 LevelInfoDefinition
		struct Dungeon
		{
			uint32_t dungeonSeed;
			WEInt widthChunks;
			SNInt depthChunks;
			bool isArtifactDungeon;

			void init(uint32_t dungeonSeed, WEInt widthChunks, SNInt depthChunks, bool isArtifactDungeon);
		};
	private:
		Type type;
		Interior interior;
		Dungeon dungeon;

		void init(Type type);
	public:
		InteriorGenerationInfo();

		void initInterior(std::string &&mifName, std::string &&displayName, bool isPalace,
			const std::optional<bool> &rulerIsMale);
		void initDungeon(uint32_t dungeonSeed, WEInt widthChunks, SNInt depthChunks, bool isArtifactDungeon);

		Type getType() const;
		const Interior &getInterior() const;
		const Dungeon &getDungeon() const;
	};

	// Input: 1 .MIF + 1 weather .INF
	// Output: 1 LevelDefinition + 1 LevelInfoDefinition
	struct CityGenerationInfo
	{
		std::string mifName;
		uint32_t citySeed;
		bool isPremade;

		// Affects which types of city blocks are used at generation start.
		Buffer<uint8_t> reservedBlocks;
		
		// Generation offset from city origin.
		WEInt blockStartPosX;
		SNInt blockStartPosY;

		int cityBlocksPerSide;
		
		void init(std::string &&mifName, uint32_t citySeed, bool isPremade,
			Buffer<uint8_t> &&reservedBlocks, WEInt blockStartPosX, SNInt blockStartPosY,
			int cityBlocksPerSide);
	};

	// Input: 70 .RMD files (from asset library) + 1 weather .INF
	// Output: 70 LevelDefinitions + 1 LevelInfoDefinition
	struct WildGenerationInfo
	{
		Buffer2D<WildBlockID> wildBlockIDs;
		uint32_t fallbackSeed;

		void init(Buffer2D<WildBlockID> &&wildBlockIDs, uint32_t fallbackSeed);
	};

	class Interior
	{
	private:
		// @todo: interior type (shop, mage's guild, dungeon, etc.)?
		// - InteriorDefinition? Contains isPalace, etc.?
	public:
		void init();
	};

	class City
	{
	private:
		// Generation infos for building interiors.
		std::vector<InteriorGenerationInfo> interiorGenInfos;
	public:
		const InteriorGenerationInfo &getInteriorGenerationInfo(int index) const;

		// The returned index should be assigned to the associated transition voxel definition.
		int addInteriorGenerationInfo(InteriorGenerationInfo &&generationInfo);
	};

	class Wild
	{
	private:
		// Each index is a wild chunk pointing into the map's level definitions.
		Buffer2D<int> levelDefIndices;
		uint32_t fallbackSeed; // I.e. the world map location seed.

		std::vector<InteriorGenerationInfo> interiorGenInfos; // Building interiors and wild dens.

		// @todo: interior gen info (index?) for when player creates a wall on water.
	public:
		void init(Buffer2D<int> &&levelDefIndices, uint32_t fallbackSeed);

		int getLevelDefIndex(const ChunkInt2 &chunk) const;
		const InteriorGenerationInfo &getInteriorGenerationInfo(int index) const;

		// The returned index should be assigned to the associated transition voxel definition.
		int addInteriorGenerationInfo(InteriorGenerationInfo &&generationInfo);
	};
private:
	Buffer<LevelDefinition> levels;
	Buffer<LevelInfoDefinition> levelInfos; // Each can be used by one or more levels.
	Buffer<int> levelInfoMappings; // Level info pointed to by each level.
	Buffer<LevelDouble2> startPoints;
	std::optional<int> startLevelIndex;

	// World-type-specific data.
	WorldType worldType;
	Interior interior;
	City city;
	Wild wild;

	void init(WorldType worldType);
	bool initInteriorLevels(const MIFFile &mif, bool isPalace, const std::optional<bool> &rulerIsMale,
		const CharacterClassLibrary &charClassLibrary, const EntityDefinitionLibrary &entityDefLibrary,
		const BinaryAssetLibrary &binaryAssetLibrary, TextureManager &textureManager);
	bool initDungeonLevels(const MIFFile &mif, WEInt widthChunks, SNInt depthChunks,
		bool isArtifactDungeon, ArenaRandom &random, const CharacterClassLibrary &charClassLibrary,
		const EntityDefinitionLibrary &entityDefLibrary, const BinaryAssetLibrary &binaryAssetLibrary,
		TextureManager &textureManager, LevelInt2 *outStartPoint);
	bool initCityLevel(const MIFFile &mif, uint32_t citySeed, bool isPremade,
		const BufferView<const uint8_t> &reservedBlocks, WEInt blockStartPosX, SNInt blockStartPosY,
		int cityBlocksPerSide, const INFFile &inf, const CharacterClassLibrary &charClassLibrary,
		const EntityDefinitionLibrary &entityDefLibrary, const BinaryAssetLibrary &binaryAssetLibrary,
		TextureManager &textureManager);
	void initStartPoints(const MIFFile &mif);
public:
	bool initInterior(const InteriorGenerationInfo &generationInfo,
		const CharacterClassLibrary &charClassLibrary, const EntityDefinitionLibrary &entityDefLibrary,
		const BinaryAssetLibrary &binaryAssetLibrary, TextureManager &textureManager);
	bool initCity(const CityGenerationInfo &generationInfo, ClimateType climateType, WeatherType weatherType,
		const CharacterClassLibrary &charClassLibrary, const EntityDefinitionLibrary &entityDefLibrary,
		const BinaryAssetLibrary &binaryAssetLibrary, TextureManager &textureManager);
	bool initWild(const WildGenerationInfo &generationInfo, ClimateType climateType, WeatherType weatherType,
		const BinaryAssetLibrary &binaryAssetLibrary);

	// Gets the initial level index for the map (if any).
	const std::optional<int> &getStartLevelIndex() const;

	// Starting positions for the player.
	int getStartPointCount() const;
	const LevelDouble2 &getStartPoint(int index) const;

	// Gets the number of levels in the map.
	int getLevelCount() const;

	// This has different semantics based on the world type. For interiors, levels are separated by
	// level up/down transitions. For a city, there is only one level. For the wilderness, it gets
	// the level associated with a wild chunk whose index is acquired by querying some wild chunk
	// coordinate.
	const LevelDefinition &getLevel(int index) const;
	const LevelInfoDefinition &getLevelInfoForLevel(int levelIndex) const;

	WorldType getWorldType() const;
	const Interior &getInterior() const;
	const City &getCity() const;
	const Wild &getWild() const;
};

#endif
