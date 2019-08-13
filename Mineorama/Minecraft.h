#pragma once

#include <cstdint>

constexpr int BLOCKS_PER_CHUNK = 16;

enum class Tag : int8_t {
	Unknown = 0,
	Data2D = 45,
	Data2DLegacy = 46,
	SubChunkPrefix = 47,
	LegacyTerrain = 48,
	BlockEntity = 49,
	Entity = 50,
	PendingTicks = 51,
	BlockExtraData = 52,
	BiomeState = 53,
	FinalizedState = 54,
	Version = 118
};
