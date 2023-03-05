#ifndef RENDER_SHADER_UTILS_H
#define RENDER_SHADER_UTILS_H

enum class VertexShaderType
{
	Voxel,
	SwingingDoor,
	SlidingDoor,
	RaisingDoor,
	SplittingDoor,
	Entity
};

enum class PixelShaderType
{
	Opaque,
	OpaqueWithAlphaTestLayer,
	OpaqueWithFade,
	AlphaTested,
	AlphaTestedWithVariableTexCoordUMin, // Sliding doors.
	AlphaTestedWithVariableTexCoordVMin, // Raising doors.
	AlphaTestedWithPalette // Citizens.
};

enum class TextureSamplingType
{
	Default,
	ScreenSpaceRepeatY // Chasms.
};

#endif