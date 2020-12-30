#ifndef TEXTURE_BUILDER_H
#define TEXTURE_BUILDER_H

#include <cstdint>
#include <optional>

#include "TextureUtils.h"

#include "components/utilities/Buffer2D.h"

// Intermediate texture data for initializing renderer-specific textures (voxels, entities, UI, etc.).

class TextureBuilder
{
public:
	enum class Type
	{
		Paletted,
		TrueColor
	};

	struct PalettedTexture
	{
		Buffer2D<uint8_t> texels;
		std::optional<PaletteID> paletteID;

		void init(int width, int height, const uint8_t *texels, const std::optional<PaletteID> &paletteID);
	};

	struct TrueColorTexture
	{
		Buffer2D<uint32_t> texels;

		void init(int width, int height, const uint32_t *texels);
	};
private:
	Type type;
	PalettedTexture paletteTexture;
	TrueColorTexture trueColorTexture;
public:
	TextureBuilder();

	void initPaletted(int width, int height, const uint8_t *texels, const std::optional<PaletteID> &paletteID);
	void initPaletted(int width, int height, const uint8_t *texels);
	void initTrueColor(int width, int height, const uint32_t *texels);

	Type getType() const;
	const PalettedTexture &getPaletted() const;
	const TrueColorTexture &getTrueColor() const;
};

#endif
